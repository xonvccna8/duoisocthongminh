# Tạo bản ảnh NHỎ để nhúng vào firmware ESP32: web/img/*.jpg -> web/img_fw/*.webp
# Firmware phải vừa phân vùng mặc định của Arduino IDE (1,25MB cho chương trình), nên ảnh
# trong firmware được thu nhỏ + nén WebP. Trang trên Vercel vẫn dùng ảnh gốc trong web/img.
# Được tools/build_web.ps1 gọi tự động; cần Pillow (pip install pillow).
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC, DST = ROOT / 'web' / 'img', ROOT / 'web' / 'img_fw'
QUALITY = 34
# Chiều rộng tối đa (px) theo kích thước ảnh hiển thị trên trang
WIDTH = {'hero': 480, 'du_pile': 360, 'cam_tree': 320, 'cam_sq1': 320, 'cam_sq2': 320, 'cam_sq3': 320,
         'sq_eat': 240, 'sq_branch': 240, 'du_open': 220, 'du_one': 200, 'sq_face': 200}

DST.mkdir(exist_ok=True)
for old in DST.glob('*.webp'):
    old.unlink()
total = 0
for f in sorted(SRC.glob('*.jpg')):
    im = Image.open(f).convert('RGB')
    w = WIDTH.get(f.stem, 320)
    if im.width > w:
        im = im.resize((w, round(im.height * w / im.width)), Image.LANCZOS)
    out = DST / (f.stem + '.webp')
    im.save(out, 'WEBP', quality=QUALITY, method=6)
    total += out.stat().st_size
print(f'Anh firmware: {len(list(DST.glob("*.webp")))} anh, {total / 1024:.1f} KB')
