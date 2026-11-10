#!/usr/bin/env python3
"""Lap danh sach ghi nguon (tac gia, giay phep, link) cho cac anh DA DUNG trong dataset/ -> ai_soc/NGUON_ANH.csv
va tong ket so anh theo giay phep. Cach lam giong ai_camera/tools/make_attribution.py (du an Huynh Thai Nguyen).

  python ghi_nguon.py
"""

import collections
import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW = ROOT / "raw_images"
FIELDS = ["pageid", "group", "source", "file", "title", "license", "artist", "page_url"]


def main():
    used = {p.stem for p in (ROOT / "dataset" / "images").rglob("*.jpg")}
    rows = []
    for meta in sorted(RAW.glob("metadata*.csv")):
        with meta.open(encoding="utf-8") as fh:
            for r in csv.DictReader(fh, fieldnames=FIELDS):
                if r["pageid"] != "pageid" and f"{r['group']}_{r['pageid']}" in used:
                    rows.append(r)
    out = ROOT / "NGUON_ANH.csv"
    with out.open("w", newline="", encoding="utf-8-sig") as fh:  # utf-8-sig: Excel mo dung tieng Viet
        w = csv.writer(fh)
        w.writerow(["anh", "nhom", "tieu_de", "giay_phep", "tac_gia", "link_goc"])
        for r in sorted(rows, key=lambda r: (r["group"], r["pageid"])):
            w.writerow([f"{r['group']}_{r['pageid']}.jpg", r["group"], r["title"], r["license"], r["artist"], r["page_url"]])
    lic = collections.Counter(r["license"] for r in rows)
    print(f"{len(rows)} anh -> {out}")
    for k, v in lic.most_common():
        print(f"  {v:5d}  {k}")


if __name__ == "__main__":
    main()
