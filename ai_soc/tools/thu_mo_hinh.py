#!/usr/bin/env python3
"""Thu mo hinh soc_yolo26n tren anh mau (mac dinh: web/img), in diem soc tung anh va ve khung
vao logs/thu_mo_hinh.jpg de xem bang mat.

  python thu_mo_hinh.py
  python thu_mo_hinh.py --src duong/dan/thu_muc_anh --conf 0.25
"""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
MODEL = ROOT / "models" / "soc_yolo26n.pt"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", default=str(ROOT.parent / "web" / "img"))
    ap.add_argument("--conf", type=float, default=0.25)
    ap.add_argument("--model", default=str(MODEL))
    args = ap.parse_args()

    from ultralytics import YOLO

    model = YOLO(args.model)
    files = sorted(p for p in Path(args.src).iterdir() if p.suffix.lower() in (".jpg", ".jpeg", ".png", ".webp"))
    tiles = []
    for f in files:
        r = model.predict(str(f), conf=args.conf, verbose=False)[0]
        scores = [float(s) for s in r.boxes.conf.cpu()]
        best = max(scores, default=0.0)
        print(f"{f.name:16s} soc={len(scores)}  diem cao nhat={best * 100:5.1f}")
        img = Image.open(f).convert("RGB")
        d = ImageDraw.Draw(img)
        for (x0, y0, x1, y1), s in zip(r.boxes.xyxy.cpu().tolist(), scores):
            d.rectangle([x0, y0, x1, y1], outline="#dc2626", width=max(3, img.width // 150))
            d.text((x0 + 4, y0 + 2), f"soc {s:.2f}", fill="#dc2626")
        img.thumbnail((360, 360))
        tile = Image.new("RGB", (360, 380), "white")
        tile.paste(img, ((360 - img.width) // 2, 0))
        ImageDraw.Draw(tile).text((6, 364), f"{f.name}  {best * 100:.0f}", fill="black")
        tiles.append(tile)
    cols = 4
    sheet = Image.new("RGB", (360 * cols, 380 * ((len(tiles) + cols - 1) // cols)), "white")
    for i, t in enumerate(tiles):
        sheet.paste(t, (360 * (i % cols), 380 * (i // cols)))
    out = ROOT / "logs" / "thu_mo_hinh.jpg"
    sheet.save(out, quality=85)
    print("Da luu", out)


if __name__ == "__main__":
    main()
