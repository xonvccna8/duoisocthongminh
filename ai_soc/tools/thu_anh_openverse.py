#!/usr/bin/env python3
"""Thu anh SOC (va anh nen) giay phep Creative Commons tu Openverse (api.openverse.org).

Cach lam giong ai_camera/tools/collect_openverse.py cua du an Huynh Thai Nguyen.
Ghi raw_images/metadata_openverse.csv (cung dinh dang metadata.csv), co tac gia, giay phep,
trang goc de ghi nguon. Openverse an danh: 20 tim kiem/phut, 200/ngay; proxy anh 1000/ngay.
Ket qua tim kiem luu o raw_images/openverse_results.json de chay lai khong ton han muc.

  python thu_anh_openverse.py --pages 5
"""

import argparse
import concurrent.futures as cf
import csv
import io
import json
import threading
import time
from pathlib import Path

import requests

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "raw_images"
UA = "DuoiSocDatasetBot/1.0 (student project Vietnam) python-requests/2.34"
QUERIES = [
    ("soc", "squirrel"), ("soc", "squirrel tree"), ("soc", "squirrel eating"), ("soc", "red bellied squirrel"),
    ("soc", "Callosciurus"), ("soc", "Pallas squirrel"), ("soc", "palm squirrel"), ("soc", "giant squirrel"),
    ("soc", "tree squirrel branch"),
    ("nen", "durian tree"), ("nen", "durian fruit tree"), ("nen", "fruit orchard"), ("nen", "bird branch tree"),
]
SKIP_WORDS = ("map", "diagram", "chart", "logo", "poster", "illustration", "drawing", "cartoon", "toy", "plush",
              "statue", "sculpture", "skull", "taxiderm")

session = requests.Session()
session.headers["User-Agent"] = UA


def search(args, seen):
    jobs = []
    for group, q in QUERIES:
        for page in range(1, args.pages + 1):
            r = None
            for attempt in range(5):
                try:
                    r = session.get("https://api.openverse.org/v1/images/",
                                    params={"q": q, "page": page, "page_size": 20, "mature": "false"}, timeout=60)
                except requests.RequestException as exc:   # may chu cham: cho roi thu lai
                    print(f"  {q} p{page}: {type(exc).__name__}, thu lai", flush=True)
                    time.sleep(10 * (attempt + 1))
                    continue
                if r.status_code == 429:
                    time.sleep(int(r.headers.get("Retry-After", 30)))
                    continue
                break
            time.sleep(3.5)  # gioi han an danh 20/phut
            if r is None or r.status_code != 200:
                print(f"  {q} p{page}: HTTP {r.status_code if r is not None else 'loi mang'}")
                break
            results = r.json().get("results", [])
            for it in results:
                key = f"ov_{it['id']}"
                title = (it.get("title") or "").lower()
                if key in seen or any(w in title for w in SKIP_WORDS):
                    continue
                if it.get("width") and it.get("height") and min(it["width"], it["height"]) < 400:
                    continue
                seen.add(key)
                jobs.append((group, q, key, it))
            print(f"[{group}] {q} trang {page}: {len(results)} ket qua, tong cho tai {len(jobs)}", flush=True)
            if len(results) < 20:
                break
    return jobs


def download(args, jobs, writer):
    # Xen ke theo tu khoa de cac nhom can bang khi bi gioi han 1000 anh/ngay.
    by_query = {}
    for j in jobs:
        by_query.setdefault(j[1], []).append(j)
    order = []
    while any(by_query.values()):
        for q in list(by_query):
            if by_query[q]:
                order.append(by_query[q].pop(0))
    order = order[: args.max_downloads]
    limited = threading.Event()

    def fetch(job):
        group, q, key, it = job
        if limited.is_set():
            return None
        try:
            # Proxy anh cua Openverse (Flickr chan truy cap truc tiep tu script).
            r = session.get(it["thumbnail"] + "?full_size=true", timeout=40)
            if r.status_code == 429:
                limited.set()
                return None
            if r.status_code != 200 or len(r.content) < 15000 or not r.headers.get("content-type", "").startswith("image"):
                return None
            from PIL import Image

            img = Image.open(io.BytesIO(r.content)).convert("RGB")
            if min(img.size) < 400:
                return None
            img.thumbnail((1280, 1280))
            dest = OUT / group / f"{key}.jpg"
            dest.parent.mkdir(parents=True, exist_ok=True)
            img.save(dest, quality=90)
            lic = f"CC {it.get('license', '').upper()} {it.get('license_version') or ''}".strip()
            return [key, group, f"openverse:{q}", dest.relative_to(OUT).as_posix(), it.get("title", ""),
                    lic, it.get("creator", ""), it.get("foreign_landing_url", "")]
        except Exception:
            return None

    ok = 0
    with cf.ThreadPoolExecutor(3) as pool:
        for i, row in enumerate(pool.map(fetch, order)):
            if row:
                writer.writerow(row)
                ok += 1
            if i % 50 == 0:
                print(f"  tai {ok}/{i + 1}", flush=True)
    print(f"Openverse: tai {ok}/{len(order)} anh" + (" (dung vi het han muc ngay)" if limited.is_set() else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pages", type=int, default=5)
    ap.add_argument("--max-downloads", type=int, default=950, help="proxy anh an danh: 1000/ngay")
    args = ap.parse_args()
    OUT.mkdir(exist_ok=True)
    meta = OUT / "metadata_openverse.csv"
    seen = set()
    if meta.exists():
        with meta.open(encoding="utf-8") as fh:
            seen = {row[0] for row in csv.reader(fh) if row}
    new = not meta.exists() or meta.stat().st_size == 0
    with meta.open("a", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        if new:
            w.writerow(["pageid", "group", "source", "file", "title", "license", "artist", "page_url"])
        cache = OUT / "openverse_results.json"
        if cache.exists():
            jobs = [tuple(j) for j in json.loads(cache.read_text(encoding="utf-8"))]
            jobs = [j for j in jobs if j[2] not in seen]
            print(f"Dung lai {len(jobs)} ket qua tim kiem da luu")
        else:
            jobs = search(args, seen)
            if len(jobs) >= 100:  # may chu loi thi khong luu, lan sau tim lai
                cache.write_text(json.dumps(jobs), encoding="utf-8")
        download(args, jobs, w)


if __name__ == "__main__":
    main()
