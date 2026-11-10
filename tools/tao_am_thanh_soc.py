#!/usr/bin/env python3
"""Tao bo TIENG XUA SOC nhung san trong firmware (khong can the nho).

Tieng ke thu cua soc (meo, cho, dieu hau, cu meo) giay phep tu do tu Wikimedia Commons:
tai ve am_thanh_soc/goc/, cat doan to nhat <= 3 giay, doi sang 16 kHz mono, nen mu-law 8 bit
-> duoi_soc_web/am_thanh.h (mang trong bo nho flash), am_thanh_soc/nghe_thu/*.wav de nghe thu,
am_thanh_soc/NGUON_AM_THANH.md de ghi nguon.

  python tools/tao_am_thanh_soc.py
"""

import re
import time
from pathlib import Path

import numpy as np
import requests
import soundfile as sf
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "am_thanh_soc"
HEADER = ROOT / "duoi_soc_web" / "am_thanh.h"
RATE = 16000
MAX_SEC = 3.0
UA = "DuoiSocSoundBot/1.0 (student project Vietnam) python-requests"

# (ten trong firmware, ten tieng Viet, pageid Wikimedia Commons). Thu tu: meo truoc (khi co soc phat tieng meo dau tien)
SOUNDS = [
    ("meo_keu", "Mèo kêu meo meo", 31896202),
    ("meo_gao", "Mèo gầm gừ", 46435510),
    ("cho_sua", "Chó sủa", 17851672),
    ("meo_khe", "Mèo khè (phì)", 31903363),
    ("dieu_hau", "Diều hâu đuôi đỏ kêu", 23927983),
    ("meo_keu_2", "Mèo con kêu", 20319369),
    ("cu_meo", "Cú sừng kêu", 77529556),
    ("cho_sua_2", "Chó sủa dữ", 32225132),
    ("meo_ri", "Mèo kêu rên", 11732040),
    ("dieu_hau_2", "Diều hâu thét", 50055694),
]

session = requests.Session()
session.headers["User-Agent"] = UA


def info(pageid):
    for _ in range(6):
        r = session.get("https://commons.wikimedia.org/w/api.php", params={
            "action": "query", "format": "json", "formatversion": 2, "pageids": pageid, "prop": "imageinfo",
            "iiprop": "url|extmetadata", "iiextmetadatafilter": "LicenseShortName|Artist"}, timeout=30)
        if r.status_code == 200:
            p = r.json()["query"]["pages"][0]
            ii = p["imageinfo"][0]
            em = ii.get("extmetadata", {})
            return {"title": p["title"][5:], "url": ii["url"], "page": ii["descriptionurl"],
                    "lic": em.get("LicenseShortName", {}).get("value", ""),
                    "artist": re.sub(r"<[^>]+>", "", em.get("Artist", {}).get("value", "")).strip()[:120]}
        time.sleep(15)
    raise RuntimeError(f"Commons khong tra loi cho {pageid}")


def load(path):
    data, rate = sf.read(str(path), dtype="float32", always_2d=True)   # wav/ogg/flac/mp3 (libsndfile >= 1.1)
    x = data.mean(axis=1)
    g = np.gcd(int(rate), RATE)
    return resample_poly(x, RATE // g, int(rate) // g).astype(np.float32)


def best_part(x):
    """Doan <= 3 giay co nang luong lon nhat, bo khoang lang 2 dau, len/xuong am 20 ms, lam to (xem loud())."""
    win = int(MAX_SEC * RATE)
    if len(x) > win:
        e = np.convolve(x ** 2, np.ones(RATE // 10), "same")
        c = np.cumsum(np.concatenate([[0], e]))
        s = int(np.argmax(c[win:] - c[:-win]))
        x = x[s:s + win]
    env = np.abs(x) > 0.05 * np.abs(x).max()
    idx = np.nonzero(env)[0]
    x = x[max(0, idx[0] - 320):idx[-1] + 320]
    f = min(320, len(x) // 4)
    ramp = np.linspace(0, 1, f, dtype=np.float32)
    x[:f] *= ramp
    x[-f:] *= ramp[::-1]
    return loud(x / np.abs(x).max())


TARGET_RMS_DB = -6.0   # to nhat co the: moi tieng to nhu nhau, sat muc toi da cua loa (xua soc, khong can em tai)


def loud(x):
    """Lam to: khuech dai toi RMS -6 dBFS (toi da +18 dB), bo gioi han mem tanh de dinh khong bi vo tieng."""
    rms_db = 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-9)
    g = min(8.0, 10 ** ((TARGET_RMS_DB - rms_db) / 20))
    y = np.tanh(x * g * 1.6) / np.tanh(1.6)
    return (y / np.abs(y).max() * 0.97).astype(np.float32)


def mulaw(x):
    """Ma hoa G.711 mu-law (giai ma bang bang 256 gia tri trong firmware)."""
    s = np.clip(np.round(x * 32767), -32635, 32635).astype(np.int32)
    sign = (s < 0).astype(np.int32)
    s = np.abs(s) + 0x84
    exp = np.floor(np.log2(s)).astype(np.int32) - 7
    exp = np.clip(exp, 0, 7)
    mant = (s >> (exp + 3)) & 0x0F
    return (~((sign << 7) | (exp << 4) | mant) & 0xFF).astype(np.uint8)


def main():
    import sys
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # console Windows cp1252
    (OUT / "goc").mkdir(parents=True, exist_ok=True)
    (OUT / "nghe_thu").mkdir(exist_ok=True)
    clips, credits = [], []
    for key, name, pid in SOUNDS:
        meta = info(pid)
        src = OUT / "goc" / f"{key}{Path(meta['url']).suffix.lower()}"
        if not src.exists():
            for attempt in range(6):   # may chu Wikimedia gioi han toc do: cho roi thu lai
                r = session.get(meta["url"], timeout=60)
                if r.status_code != 429:
                    break
                time.sleep(20 * (attempt + 1))
            r.raise_for_status()
            src.write_bytes(r.content)
            time.sleep(2)
        x = best_part(load(src))
        sf.write(str(OUT / "nghe_thu" / f"{len(clips) + 1:02d}_{key}.wav"), x, RATE, subtype="PCM_16")
        clips.append((key, name, mulaw(x)))
        credits.append((len(clips), name, meta))
        print(f"{len(clips):2d}. {name:24s} {len(x) / RATE:4.1f}s  {meta['lic']:14s} {meta['title']}", flush=True)

    total = sum(len(c[2]) for c in clips)
    with HEADER.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("// TỰ SINH bởi tools/tao_am_thanh_soc.py - đừng sửa tay.\n")
        fh.write("// Tiếng kẻ thù của sóc (mèo, chó, diều hâu, cú) - nguồn: am_thanh_soc/NGUON_AM_THANH.md\n")
        fh.write(f"// {len(clips)} tiếng, 16 kHz mono, mu-law 8 bit, tổng {total // 1024} KB trong bộ nhớ flash.\n#pragma once\n#include <stdint.h>\n\n")
        for i, (key, name, data) in enumerate(clips):
            fh.write(f"static const uint8_t SND_{i}[{len(data)}] = {{  // {name}\n")
            for j in range(0, len(data), 32):
                fh.write("  " + ",".join(str(v) for v in data[j:j + 32]) + ",\n")
            fh.write("};\n")
        fh.write("\nstruct BuiltinSound { const char *name; const uint8_t *data; uint32_t len; };\n")
        fh.write("static const BuiltinSound BUILTIN_SOUNDS[] = {\n")
        for i, (key, name, data) in enumerate(clips):
            fh.write(f'  {{"{name}", SND_{i}, {len(data)}}},\n')
        fh.write("};\nstatic const int BUILTIN_COUNT = sizeof(BUILTIN_SOUNDS) / sizeof(BUILTIN_SOUNDS[0]);\n")

    with (OUT / "NGUON_AM_THANH.md").open("w", encoding="utf-8") as fh:
        fh.write("# Nguồn tiếng xua sóc (nhúng trong firmware)\n\n")
        fh.write("Tất cả lấy từ Wikimedia Commons, giấy phép tự do. Đã cắt đoạn ≤ 3 giây, đổi 16 kHz mono.\n\n")
        fh.write("| # | Tiếng | Giấy phép | Tác giả | File gốc |\n|---|---|---|---|---|\n")
        for n, name, m in credits:
            fh.write(f"| {n} | {name} | {m['lic']} | {m['artist'] or '—'} | [{m['title']}]({m['page']}) |\n")
    print(f"Da ghi {HEADER} ({total // 1024} KB) va {OUT / 'NGUON_AM_THANH.md'}")


if __name__ == "__main__":
    main()
