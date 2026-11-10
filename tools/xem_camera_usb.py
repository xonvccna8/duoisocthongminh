#!/usr/bin/env python3
"""Xem camera ESP32-S3 qua cap USB tren may tinh + AI YOLO nhan soc chay ngay tren may.

Board phai nap xem_camera_usb/xem_camera_usb.ino. Chay:
  python tools/xem_camera_usb.py --port COM7
roi mo http://localhost:8090  (nut: lat doc, lat ngang, doi do phan giai)
Diem soc giong /api/ai tren Vercel: tin cay 0,15 = 60 diem (nguong xua duoi mac dinh).
"""

import argparse
import io
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import serial
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
MODEL = ROOT / "ai_soc" / "models" / "soc_yolo26n.pt"
MAGIC = b"\xff\xaaJP"
AT60 = 0.15

state = {"jpg": None, "seq": 0, "fps": 0.0, "size": "", "score": 0, "ai_ms": 0, "err": ""}
cond = threading.Condition()
ser = None


def to_score(p):
    return round(p / AT60 * 60 if p <= AT60 else 60 + (p - AT60) / (1 - AT60) * 40)


def reader(port):
    global ser
    ser = serial.Serial(port, 2000000, timeout=1)
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False  # khoi dong lai board
    buf, times = b"", []
    while True:
        buf += ser.read(max(1, ser.in_waiting))
        if b"CAMERA_LOI" in buf:
            state["err"] = "Board bao loi camera"
            buf = b""
        i = buf.find(MAGIC)
        if i < 0:
            buf = buf[-3:]
            continue
        if len(buf) < i + 8:
            continue
        n = int.from_bytes(buf[i + 4:i + 8], "little")
        if n <= 0 or n > 600000:
            buf = buf[i + 4:]
            continue
        if len(buf) < i + 8 + n:
            continue
        jpg, buf = buf[i + 8:i + 8 + n], buf[i + 8 + n:]
        if jpg[:2] != b"\xff\xd8":
            continue
        times = [t for t in times if t > time.time() - 2] + [time.time()]
        with cond:
            state.update(raw=jpg, seq=state["seq"] + 1, fps=len(times) / 2, err="")
            cond.notify_all()


def detector():
    from ultralytics import YOLO

    model = YOLO(str(MODEL))
    last = -1
    while True:
        with cond:
            cond.wait_for(lambda: state["seq"] != last and state.get("raw"))
            raw, last = state["raw"], state["seq"]
        img = Image.open(io.BytesIO(raw)).convert("RGB")
        t0 = time.time()
        r = model.predict(img, conf=0.05, verbose=False)[0]
        ms = int((time.time() - t0) * 1000)
        best = 0.0
        d = ImageDraw.Draw(img)
        for (x0, y0, x1, y1), p in zip(r.boxes.xyxy.tolist(), r.boxes.conf.tolist()):
            best = max(best, p)
            color = "#dc2626" if to_score(p) >= 60 else "#f59e0b"
            d.rectangle([x0, y0, x1, y1], outline=color, width=4)
            d.text((x0 + 6, y0 + 4), f"soc {to_score(p)}", fill=color)
        out = io.BytesIO()
        img.save(out, "JPEG", quality=85)
        with cond:
            state.update(jpg=out.getvalue(), size=f"{img.width}x{img.height}", score=to_score(best) if best else 0,
                         ai_ms=ms)
            cond.notify_all()


PAGE = """<!doctype html><html lang="vi"><meta charset="utf-8"><title>Camera USB</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px;text-align:center}
img{max-width:100%;border-radius:8px;background:#222;min-height:240px}button{font-size:16px;margin:4px;padding:8px 14px;
border-radius:8px;border:0;background:#2563eb;color:#fff;cursor:pointer}#s{font-size:18px;margin:8px}
.soc{color:#f87171;font-weight:700}</style>
<h2>Camera ESP32-S3 (OV3660) qua USB + AI YOLO nhận sóc</h2>
<img src="/stream"><div id="s">...</div>
<button onclick="c('v')">Lật dọc</button><button onclick="c('h')">Lật ngang</button>
<button onclick="c('-')">Ảnh nhỏ hơn</button><button onclick="c('+')">Ảnh lớn hơn</button>
<script>
function c(x){fetch('/cmd?c='+encodeURIComponent(x))}
setInterval(async()=>{const s=await (await fetch('/state')).json();
document.getElementById('s').innerHTML=s.err||(s.size+' · '+s.fps.toFixed(1)+' ảnh/giây · AI '+s.ai_ms+' ms · '+
(s.score>=60?'<span class=soc>CÓ SÓC '+s.score+' điểm → xua đuổi</span>':'điểm sóc '+s.score+' (chưa đủ 60)'))},500)
</script></html>"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path == "/":
            body = PAGE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/state":
            import json

            s = {k: state[k] for k in ("fps", "size", "score", "ai_ms", "err")}
            body = json.dumps(s).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(body)
        elif self.path.startswith("/cmd?c="):
            from urllib.parse import unquote

            c = unquote(self.path.split("=", 1)[1])[:1]
            if c and c in "vh+-" and ser:
                ser.write(c.encode())
            self.send_response(204)
            self.end_headers()
        elif self.path == "/stream":
            self.send_response(200)
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
            self.end_headers()
            last = None
            try:
                while True:
                    with cond:
                        cond.wait_for(lambda: state["jpg"] is not None and state["jpg"] is not last, timeout=5)
                        jpg = last = state["jpg"]
                    if jpg is None:
                        continue
                    self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n" % len(jpg))
                    self.wfile.write(jpg + b"\r\n")
            except (BrokenPipeError, ConnectionResetError):
                pass
        else:
            self.send_response(404)
            self.end_headers()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="COM7")
    ap.add_argument("--http", type=int, default=8090)
    args = ap.parse_args()
    threading.Thread(target=reader, args=(args.port,), daemon=True).start()
    threading.Thread(target=detector, daemon=True).start()
    print(f"Mo http://localhost:{args.http}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", args.http), Handler).serve_forever()


if __name__ == "__main__":
    main()
