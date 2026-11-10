#!/usr/bin/env python3
"""Thu anh SOC (va anh nen vuon cay khong co soc) giay phep tu do tu Wikimedia Commons.

Cach lam giong ai_camera/tools/collect_commons.py cua du an Huynh Thai Nguyen.
Moi anh luu kem tieu de, giay phep, tac gia, URL vao raw_images/metadata.csv de ghi nguon
khi trinh bay. Chi tai ban thumbnail 960 px, gioi han toc do de ton trong may chu Wikimedia.
Chay lai se bo qua anh da tai.

  python thu_anh_commons.py --max-per-source 120
"""

import argparse
import csv
import re
import time
from pathlib import Path

import requests

API = "https://commons.wikimedia.org/w/api.php"
UA = ("DuoiSocDatasetBot/1.0 (https://commons.wikimedia.org/wiki/Commons:Reusing_content_outside_Wikimedia;"
      " student project Vietnam) python-requests/2.34")
ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "raw_images"

# (nhom, kieu, gia tri, do sau danh muc con). "soc": anh co soc; "nen": vuon cay, chim, chuot... KHONG co soc
SOURCES = [
    # Soc o Viet Nam / Dong Nam A (pha sau rieng, dua, ca phe)
    ("soc", "cat", "Callosciurus erythraeus", 1),
    ("soc", "cat", "Callosciurus finlaysonii", 1),
    ("soc", "cat", "Callosciurus notatus", 1),
    ("soc", "cat", "Callosciurus caniceps", 1),
    ("soc", "cat", "Callosciurus prevostii", 1),
    ("soc", "cat", "Callosciurus flavimanus", 1),
    ("soc", "cat", "Ratufa bicolor", 1),
    ("soc", "cat", "Ratufa indica", 1),
    ("soc", "cat", "Tamiops", 2),
    ("soc", "cat", "Menetes berdmorei", 1),
    ("soc", "cat", "Dremomys", 2),
    ("soc", "cat", "Funambulus palmarum", 1),
    ("soc", "cat", "Funambulus pennantii", 1),
    # Soc pho bien (nhieu anh, hinh dang giong soc Viet Nam)
    ("soc", "cat", "Sciurus vulgaris", 1),
    ("soc", "cat", "Sciurus carolinensis", 1),
    ("soc", "cat", "Sciurus niger", 1),
    ("soc", "search", "squirrel eating fruit tree", 0),
    ("soc", "search", "squirrel on branch", 0),
    ("soc", "search", "sóc", 0),
    # Anh nen: cay sau rieng, vuon cay an trai, canh la, chim va chuot (de AI khong nham la soc)
    ("nen", "cat", "Durio zibethinus", 1),
    ("nen", "search", "durian tree fruit", 0),
    ("nen", "search", "durian orchard", 0),
    ("nen", "search", "fruit orchard Vietnam", 0),
    ("nen", "search", "tree branches leaves canopy", 0),
    ("nen", "cat", "Pycnonotus jocosus", 1),
    ("nen", "cat", "Pycnonotus aurigaster", 1),
    ("nen", "search", "bird on tree branch", 0),
    ("nen", "cat", "Rattus rattus", 1),
    ("nen", "search", "cat on tree", 0),
]
SKIP = re.compile(r"map|diagram|chart|logo|graph|scheme|drawing|plan\b|poster|stamp|coat of arms|skull|skeleton|"
                  r"taxiderm|specimen|museum|illustration|plate\b|\.svg|\.tif|\.pdf|icon|sign\b|range\b|distribution",
                  re.I)

session = requests.Session()
session.headers["User-Agent"] = UA


def api(params, tries=6):
    params = {"format": "json", "formatversion": 2, **params}
    for attempt in range(tries):
        try:
            r = session.get(API, params=params, timeout=30)
        except requests.RequestException:
            time.sleep(5 * (attempt + 1))
            continue
        if r.status_code == 429 or r.status_code >= 500:
            wait = int(r.headers.get("Retry-After", 10 * (attempt + 1)))
            print(f"  API {r.status_code}, cho {wait}s")
            time.sleep(wait)
            continue
        r.raise_for_status()
        time.sleep(2.0)
        return r.json()
    raise RuntimeError("API khong phan hoi")


def subcats(cat, depth):
    cats, frontier = [cat], [cat]
    for _ in range(depth):
        nxt = []
        for c in frontier:
            cont = {}
            while True:
                d = api({"action": "query", "list": "categorymembers", "cmtitle": f"Category:{c}",
                         "cmtype": "subcat", "cmlimit": 500, **cont})
                for m in d.get("query", {}).get("categorymembers", []):
                    name = m["title"].split(":", 1)[1]
                    if name not in cats and not SKIP.search(name):
                        cats.append(name)
                        nxt.append(name)
                if "continue" not in d:
                    break
                cont = d["continue"]
        frontier = nxt
    return cats


def file_pages(kind, value, depth, limit):
    common = {"action": "query", "prop": "imageinfo", "iiprop": "url|mime|size|extmetadata",
              "iiurlwidth": 960, "iiextmetadatafilter": "LicenseShortName|Artist|UsageTerms"}
    if kind == "search":
        gens = [{"generator": "search", "gsrsearch": f"{value} filetype:bitmap", "gsrnamespace": 6, "gsrlimit": 50}]
    else:
        gens = [{"generator": "categorymembers", "gcmtitle": f"Category:{c}", "gcmtype": "file", "gcmlimit": 50}
                for c in subcats(value, depth)]
    for g in gens:
        cont = {}
        while True:
            d = api({**common, **g, **cont})
            for p in d.get("query", {}).get("pages", []):
                yield p
                limit -= 1
                if limit <= 0:
                    return
            if "continue" not in d or kind == "search" and limit <= 0:
                break
            cont = d["continue"]


def strip_html(s):
    return re.sub(r"<[^>]+>", "", s or "").strip()[:200]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--max-per-source", type=int, default=120)
    ap.add_argument("--start", type=int, default=0, help="bo qua N nguon dau (chay tiep)")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    meta_path = OUT / "metadata.csv"
    seen = set()
    if meta_path.exists():
        with meta_path.open(encoding="utf-8") as fh:
            seen = {row[0] for row in csv.reader(fh) if row}
    new_file = not meta_path.exists() or meta_path.stat().st_size == 0
    with meta_path.open("a", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        if new_file:
            w.writerow(["pageid", "group", "source", "file", "title", "license", "artist", "page_url"])
        for group, kind, value, depth in SOURCES[args.start:]:
            got = 0
            print(f"[{group}] {kind}: {value}", flush=True)
            try:
                for p in file_pages(kind, value, depth, args.max_per_source * 3):
                    if got >= args.max_per_source:
                        break
                    pid = str(p.get("pageid"))
                    ii = (p.get("imageinfo") or [{}])[0]
                    if pid in seen or SKIP.search(p.get("title", "")):
                        continue
                    if ii.get("mime") not in ("image/jpeg", "image/png", "image/webp"):
                        continue
                    if min(ii.get("width", 0), ii.get("height", 0)) < 400:
                        continue
                    lic = (ii.get("extmetadata", {}).get("LicenseShortName", {}) or {}).get("value", "")
                    if not lic:
                        continue
                    url = ii.get("thumburl") or ii.get("url")
                    dest = OUT / group / f"{pid}.jpg"
                    dest.parent.mkdir(exist_ok=True)
                    for attempt in range(4):
                        try:
                            r = session.get(url, timeout=60)
                        except requests.RequestException:
                            time.sleep(5)
                            continue
                        if r.status_code == 429:
                            time.sleep(int(r.headers.get("Retry-After", 15)))
                            continue
                        break
                    else:
                        continue
                    if r.status_code != 200 or len(r.content) < 10000:
                        continue
                    dest.write_bytes(r.content)
                    artist = strip_html((ii.get("extmetadata", {}).get("Artist", {}) or {}).get("value", ""))
                    w.writerow([pid, group, f"{kind}:{value}", dest.relative_to(OUT).as_posix(),
                                p.get("title"), lic, artist, ii.get("descriptionurl", "")])
                    fh.flush()
                    seen.add(pid)
                    got += 1
                    time.sleep(1.0)
            except RuntimeError as exc:
                print(f"  bo qua nguon: {exc}")
            print(f"  tai {got} anh", flush=True)
    print("Xong:", sum(1 for _ in OUT.rglob("*.jpg")), "anh")


if __name__ == "__main__":
    main()
