#!/usr/bin/env python3
"""Máy tính chạy AI YOLO nhận sóc trên GPU (RTX 4050) cho video camera của thiết bị trong mạng nhà.

Giống máy tính trạm chính của dự án Huỳnh Thái Nguyên: đọc video MJPEG http://<thiết bị>:81/stream (~15 ảnh/giây),
chạy YOLO26n (ai_soc/models/soc_yolo26n.pt) trên MỌI khung hình, vẽ khung sóc, phát lại video ở
http://localhost:8091 (xem mượt, có khung AI, điểm sóc từng khung).

  python tools/xem_camera_ai.py                    # thiết bị 192.168.1.19
  python tools/xem_camera_ai.py --host 192.168.1.20

Chỉ để XEM: thiết bị vẫn tự quyết định xua đuổi (PIR + AI trên Vercel), máy tính tắt thì thiết bị vẫn chạy.
Điểm sóc giống /api/ai trên Vercel: độ tin cậy 0,15 = 60 điểm.

Xem từ xa ~15 ảnh/giây (giống dự án Huỳnh): mở ra Internet bằng Tailscale Funnel cổng 8443
  tailscale funnel --bg --https=8443 http://127.0.0.1:8091
Trang cần khóa xem ?k=... (tự sinh, lưu ở ai_soc/xem_camera_key.txt) để người lạ không xem được camera.
"""

import argparse
import json
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import cv2
import numpy as np
import requests

ROOT = Path(__file__).resolve().parent.parent
MODEL = ROOT / "ai_soc" / "models" / "soc_yolo26n.pt"
KEY_FILE = ROOT / "ai_soc" / "xem_camera_key.txt"
KEY = ""   # khóa xem (rỗng = không cần khóa)
AT60 = 0.15

state = {"raw": None, "seq": 0, "jpg": None, "out_seq": 0, "fps_in": 0.0, "fps_ai": 0.0, "score": 0, "ai_ms": 0,
         "err": "", "size": ""}
cond = threading.Condition()
sent = {}   # người xem -> thời điểm các ảnh đã gửi tới họ (để báo số ảnh/giây người xem THẬT SỰ nhận được)


def fps_out():
    now = time.time()
    return max((len([t for t in v if t > now - 2]) / 2 for v in list(sent.values())), default=0.0)


def to_score(p):
    return round(p / AT60 * 60 if p <= AT60 else 60 + (p - AT60) / (1 - AT60) * 40)


def reader(url):
    """Đọc luồng MJPEG, tách từng ảnh JPEG theo dấu đầu FFD8 / cuối FFD9."""
    times = []
    while True:
        try:
            with requests.get(url, stream=True, timeout=(4, 8)) as r:
                r.raise_for_status()
                buf = b""
                for chunk in r.iter_content(16384):
                    buf += chunk
                    while True:
                        a = buf.find(b"\xff\xd8")
                        b = buf.find(b"\xff\xd9", a + 2) if a >= 0 else -1
                        if a < 0 or b < 0:
                            break
                        jpg, buf = buf[a:b + 2], buf[b + 2:]
                        now = time.time()
                        times = [t for t in times if t > now - 2] + [now]
                        with cond:
                            state.update(raw=jpg, seq=state["seq"] + 1, fps_in=len(times) / 2, err="")
                            cond.notify_all()
                    if len(buf) > 2_000_000:
                        buf = b""
        except Exception as exc:
            with cond:
                state["err"] = f"Không đọc được video thiết bị ({type(exc).__name__}) – đang thử lại"
            time.sleep(2)


def detector(device):
    from ultralytics import YOLO

    model = YOLO(str(MODEL))
    last, times = -1, []
    while True:
        with cond:
            cond.wait_for(lambda: state["seq"] != last and state["raw"] is not None)
            raw, last = state["raw"], state["seq"]
        img = cv2.imdecode(np.frombuffer(raw, np.uint8), cv2.IMREAD_COLOR)
        if img is None:
            continue
        t0 = time.time()
        r = model.predict(img, conf=0.05, device=device, verbose=False)[0]
        ms = (time.time() - t0) * 1000
        best = 0.0
        boxes = list(zip(r.boxes.xyxy.tolist(), r.boxes.conf.tolist()))
        for (x0, y0, x1, y1), p in boxes:
            best = max(best, p)
            sc = to_score(p)
            col = (77, 72, 229) if sc >= 60 else (18, 154, 233)   # BGR: đỏ = đủ xua, cam = nghi ngờ
            cv2.rectangle(img, (int(x0), int(y0)), (int(x1), int(y1)), col, 3)
            cv2.putText(img, f"soc {sc}", (int(x0) + 4, max(18, int(y0) - 6)), cv2.FONT_HERSHEY_SIMPLEX, 0.7, col, 2)
        # Không có khung nào để vẽ: gửi nguyên ảnh gốc của thiết bị (nhẹ hơn ~2 lần, không nén lại thêm lần nữa)
        out = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 90])[1].tobytes() if boxes else raw
        now = time.time()
        times = [t for t in times if t > now - 2] + [now]
        with cond:
            state.update(jpg=out, out_seq=state["out_seq"] + 1, score=to_score(best) if best else 0,
                         ai_ms=round(ms, 1), fps_ai=len(times) / 2, size=f"{img.shape[1]}x{img.shape[0]}")
            cond.notify_all()


PAGE = """<!doctype html><html lang="vi"><meta charset="utf-8"><title>Camera AI (GPU)</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{font-family:system-ui,sans-serif;background:#0f1a14;color:#e8f1ec;margin:0;padding:16px;text-align:center}
img{max-width:100%;border-radius:10px;background:#1b2a21;min-height:240px}#s{font-size:18px;margin:10px}
.soc{color:#ff6b6b;font-weight:700}</style>
<h2>Camera thiết bị đuổi sóc · AI YOLO chạy trên GPU máy tính</h2>
<img id="v" alt="video"><div id="s">Đang kết nối…</div>
<script>
const q=location.search;document.getElementById('v').src='/stream'+q;
setInterval(async()=>{try{const s=await (await fetch('/state'+q)).json();
document.getElementById('s').innerHTML=s.err||(s.size+' · bạn đang nhận '+s.fps_out.toFixed(1)+' ảnh/giây (thiết bị gửi '+s.fps_in.toFixed(1)+') · AI '+s.fps_ai.toFixed(1)+
' ảnh/giây ('+s.ai_ms+' ms/ảnh) · '+(s.score>=60?'<span class=soc>CÓ SÓC '+s.score+' điểm</span>':'điểm sóc '+s.score))}catch(e){}},500)
</script></html>"""


class Server(ThreadingHTTPServer):
    # Windows cho 2 chương trình cùng chiếm 1 cổng nếu bật dùng lại cổng: tắt đi để bản thứ hai báo lỗi và thoát
    allow_reuse_address = False
    daemon_threads = True

    def handle_error(self, request, client_address):
        import sys

        if not isinstance(sys.exc_info()[1], ConnectionError):   # người xem đóng trang: bình thường, không ghi nhật ký
            super().handle_error(request, client_address)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, ctype, body):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")   # trang web Vercel đọc được (vẫn cần khóa xem)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        from urllib.parse import parse_qs, urlsplit

        u = urlsplit(self.path)
        if KEY and not secrets.compare_digest(parse_qs(u.query).get("k", [""])[0], KEY):
            return self.send(403, "text/plain; charset=utf-8", "Cần link có khóa xem (?k=...)".encode())
        self.path = u.path
        if self.path == "/":
            return self.send(200, "text/html; charset=utf-8", PAGE.encode())
        if self.path == "/state":
            s = {k: state[k] for k in ("fps_in", "fps_ai", "score", "ai_ms", "err", "size")}
            s["fps_out"] = fps_out()
            return self.send(200, "application/json", json.dumps(s).encode())
        if self.path != "/stream":
            return self.send(404, "text/plain", b"")
        self.send_response(200)
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        last = -1
        me = object()
        sent[me] = []
        try:
            while True:
                with cond:
                    cond.wait_for(lambda: state["out_seq"] != last, timeout=5)
                    jpg, last = state["jpg"], state["out_seq"]
                if jpg:
                    self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n" % len(jpg))
                    self.wfile.write(jpg + b"\r\n")
                    now = time.time()
                    sent[me] = [t for t in sent[me] if t > now - 2] + [now]
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        finally:
            sent.pop(me, None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="192.168.1.19", help="địa chỉ IP thiết bị trong mạng nhà")
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--device", default="", help="0 = GPU, cpu = CPU (mặc định: GPU nếu có)")
    ap.add_argument("--no-key", action="store_true", help="không cần khóa xem (chỉ dùng khi không mở ra Internet)")
    args = ap.parse_args()
    global KEY
    if not args.no_key:
        if not KEY_FILE.exists():
            KEY_FILE.write_text(secrets.token_urlsafe(12), encoding="utf-8")
        KEY = KEY_FILE.read_text(encoding="utf-8").strip()
    try:
        server = Server(("127.0.0.1", args.port), Handler)
    except OSError:
        print(f"Cong {args.port} da co chuong trinh khac (dang chay roi?) - thoat", flush=True)
        return
    import torch

    device = args.device or (0 if torch.cuda.is_available() else "cpu")
    threading.Thread(target=reader, args=(f"http://{args.host}:81/stream",), daemon=True).start()
    threading.Thread(target=detector, args=(device,), daemon=True).start()
    print(f"Mo http://localhost:{args.port}/{'?k=' + KEY if KEY else ''}  (AI tren {'GPU' if device != 'cpu' else 'CPU'})",
          flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
