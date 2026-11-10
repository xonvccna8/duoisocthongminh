#!/usr/bin/env python3
"""Gan nhan tu dong anh raw_images/ bang Grounding DINO (phat hien theo cau chu "squirrel").

Cach lam giong ai_camera/tools/autolabel.py cua du an Huynh Thai Nguyen, nhung chi 1 lop:
  0 soc   - con soc (moi loai: soc bung do, soc chuot, soc dat...)

Bo loc anh (truoc mo hinh): CLIP chi giu anh chup that (soc, vuon cay, chim...); bo tranh ve,
hoat hinh, thu nhoi bong, tieu ban, anh den trang, ban do...
Bo loc khung (sau mo hinh):
  - Bo khung qua nho (< 0,3% anh), khung gan ca anh (> 90%) va trung lap (NMS).
  - Anh nhom "soc" khong co khung nao thi bo (khong ro soc o dau).
  - Anh nhom "nen" ma Grounding DINO lai thay soc ro rang thi bo (khong chac la anh nen).
  - Anh nhom "nen" con lai la anh KHONG co soc: giup AI khong bao nham la, trai, chim, chuot.
Ket qua: dataset/ (YOLO), preview/ (anh ve khung de kiem tra bang mat).

  python gan_nhan.py                    # toan bo
  python gan_nhan.py --limit 40         # thu nhanh
  python gan_nhan.py --resume           # chay tiep sau khi bi dung (giu anh da gan nhan)
"""

import argparse
import csv
import random
import shutil
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
RAW = ROOT / "raw_images"
DATASET = ROOT / "dataset"
PREVIEW = ROOT / "preview"
CLASSES = ["soc"]
META_FIELDS = ["pageid", "group", "source", "file", "title", "license", "artist", "page_url"]
PROMPT = "squirrel."
MIN_SCORE = 0.35
NEN_REJECT = 0.45      # anh nen ma thay soc >= muc nay thi bo anh
CLIP_KEEP = ["a photo of a squirrel", "a photo of a squirrel on a tree branch", "a photo of a squirrel eating",
             "a photo of a squirrel on the ground", "a photo of a durian tree with fruit",
             "a photo of fruit trees in an orchard", "a photo of tree branches and leaves",
             "a photo of a bird on a branch", "a photo of a rat", "a photo of a cat in a tree"]
CLIP_REJECT = ["a drawing or painting of an animal", "a cartoon", "a stuffed toy animal", "an animal skull or skeleton",
               "a museum specimen", "a map or a diagram", "a black and white photograph", "a page of text",
               "a photo of food on a plate", "a postage stamp"]
MAX_BOX_AREA = 0.90


def nms(boxes, scores, iou_thr=0.5):
    order = np.argsort(scores)[::-1]
    keep = []
    while len(order):
        i = order[0]
        keep.append(i)
        xx0 = np.maximum(boxes[i, 0], boxes[order[1:], 0])
        yy0 = np.maximum(boxes[i, 1], boxes[order[1:], 1])
        xx1 = np.minimum(boxes[i, 2], boxes[order[1:], 2])
        yy1 = np.minimum(boxes[i, 3], boxes[order[1:], 3])
        inter = np.clip(xx1 - xx0, 0, None) * np.clip(yy1 - yy0, 0, None)
        area = lambda b: (b[:, 2] - b[:, 0]) * (b[:, 3] - b[:, 1])
        iou = inter / (area(boxes[[i]]) + area(boxes[order[1:]]) - inter + 1e-9)
        order = order[1:][iou < iou_thr]
    return keep


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", default="IDEA-Research/grounding-dino-base")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--seed", type=int, default=17)
    ap.add_argument("--device", default="", help="cuda | cpu (mac dinh: cuda neu co)")
    ap.add_argument("--max-side", type=int, default=800, help="thu nho anh dua vao mo hinh (giam tai GPU)")
    ap.add_argument("--pause", type=float, default=0.1, help="nghi giua cac anh de GPU laptop bot nong")
    ap.add_argument("--resume", action="store_true", help="giu dataset/ cu, chi xet anh chua xet")
    args = ap.parse_args()

    import torch
    from transformers import AutoModelForZeroShotObjectDetection, AutoProcessor, CLIPModel, CLIPProcessor

    device = args.device or ("cuda" if torch.cuda.is_available() else "cpu")
    clip = CLIPModel.from_pretrained("openai/clip-vit-base-patch16").to(device).eval()
    clip_proc = CLIPProcessor.from_pretrained("openai/clip-vit-base-patch16")
    prompts = CLIP_KEEP + CLIP_REJECT

    def relevant(img):
        arr = np.asarray(img.convert("HSV").resize((96, 96)), dtype=np.float32)
        if arr[..., 1].mean() < 18:  # anh den trang / gan xam
            return False, "grayscale"
        inp = clip_proc(text=prompts, images=img, return_tensors="pt", padding=True).to(device)
        with torch.no_grad():
            probs = clip(**inp).logits_per_image.softmax(-1)[0].cpu().numpy()
        return float(probs[: len(CLIP_KEEP)].sum()) >= 0.5, prompts[int(probs.argmax())]

    processor = AutoProcessor.from_pretrained(args.model)
    model = AutoModelForZeroShotObjectDetection.from_pretrained(args.model).to(device).eval()
    print("Grounding DINO tren", device, flush=True)

    rows, seen = [], set()
    for meta in sorted(RAW.glob("metadata*.csv")):
        with meta.open(encoding="utf-8") as fh:
            for r in csv.DictReader(fh, fieldnames=META_FIELDS):
                if r["pageid"] == "pageid":
                    continue  # dong tieu de
                if r["pageid"] not in seen and (RAW / r["file"]).exists():
                    seen.add(r["pageid"])
                    rows.append(r)
    random.Random(args.seed).shuffle(rows)
    if args.limit:
        rows = rows[: args.limit]

    if not args.resume:
        for d in (DATASET, PREVIEW):
            if d.exists():
                shutil.rmtree(d)
    for split in ("train", "val", "test"):
        (DATASET / "images" / split).mkdir(parents=True, exist_ok=True)
        (DATASET / "labels" / split).mkdir(parents=True, exist_ok=True)
    PREVIEW.mkdir(exist_ok=True)

    boxes_total, empty, kept, skipped, report = 0, 0, 0, {}, []
    done = set()
    seen_log = DATASET / "da_xet.txt"   # moi anh da xet (giu hay bo), de --resume khong xet lai
    if args.resume:
        if seen_log.exists():
            done = set(seen_log.read_text(encoding="utf-8").split())
        for split in ("train", "val", "test"):
            for lab in sorted((DATASET / "labels" / split).glob("*.txt")):
                if not (DATASET / "images" / split / f"{lab.stem}.jpg").exists():
                    continue
                nb = sum(1 for line in lab.read_text().splitlines() if line.strip())
                report.append({"image": lab.stem, "split": split, "group": lab.stem.split("_")[0], "boxes": nb})
                done.add(lab.stem)
                boxes_total += nb
                empty += nb == 0
        kept = len(report)
        print(f"Chay tiep: da co {kept} anh trong dataset/, {len(done)} anh da xet", flush=True)
    log_fh = seen_log.open("a", encoding="utf-8")
    for n, row in enumerate(rows):
        stem = f"{row['group']}_{row['pageid']}"
        if stem in done:
            continue
        log_fh.write(stem + "\n")
        log_fh.flush()
        if n % 20 == 0 and device == "cuda":
            torch.cuda.empty_cache()  # anh nhieu kich thuoc lam bo nho GPU phinh to, laptop 6 GB bi cham han
        try:
            img = Image.open(RAW / row["file"]).convert("RGB")
        except Exception:
            skipped["bad_file"] = skipped.get("bad_file", 0) + 1
            continue
        W, H = img.size
        scale = min(1.0, args.max_side / max(W, H))
        small = img.resize((round(W * scale), round(H * scale))) if scale < 1 else img
        ok, why = relevant(small)
        if not ok:
            skipped[why] = skipped.get(why, 0) + 1
            continue
        time.sleep(args.pause)
        inputs = processor(images=small, text=PROMPT, return_tensors="pt").to(device)
        with torch.no_grad():
            out = model(**inputs)
        res = processor.post_process_grounded_object_detection(
            out, inputs.input_ids, threshold=0.28, text_threshold=0.25, target_sizes=[(H, W)])[0]
        del out, inputs
        dets, best = [], 0.0
        for box, score in zip(res["boxes"].cpu().numpy(), res["scores"].cpu().numpy()):
            best = max(best, float(score))
            if score < MIN_SCORE:
                continue
            x0, y0, x1, y1 = np.clip(box, 0, [W, H, W, H])
            area = (x1 - x0) * (y1 - y0)
            if area < 0.003 * W * H or area > MAX_BOX_AREA * W * H:
                continue
            dets.append((float(score), [x0, y0, x1, y1]))
        final = []
        if dets:
            keep = nms(np.array([d[1] for d in dets]), np.array([d[0] for d in dets]))
            final = [dets[i] for i in keep][:10]

        if row["group"] == "soc" and not final:
            skipped["soc_no_box"] = skipped.get("soc_no_box", 0) + 1
            continue
        if row["group"] == "nen":
            if best >= NEN_REJECT:
                skipped["nen_co_soc"] = skipped.get("nen_co_soc", 0) + 1
                continue
            final = []
        kept += 1
        split = "test" if kept % 10 == 0 else "val" if kept % 10 == 1 else "train"
        img.save(DATASET / "images" / split / f"{stem}.jpg", quality=92)
        with (DATASET / "labels" / split / f"{stem}.txt").open("w") as fh:
            for score, (x0, y0, x1, y1) in final:
                fh.write(f"0 {(x0 + x1) / 2 / W:.6f} {(y0 + y1) / 2 / H:.6f} {(x1 - x0) / W:.6f} {(y1 - y0) / H:.6f}\n")
                boxes_total += 1
        if not final:
            empty += 1
        report.append({"image": stem, "split": split, "group": row["group"], "boxes": len(final)})

        draw = ImageDraw.Draw(img)
        for score, b in final:
            draw.rectangle(b, outline="#dc2626", width=max(2, W // 250))
            draw.text((b[0] + 4, b[1] + 2), f"soc {score:.2f}", fill="#dc2626")
        img.thumbnail((480, 480))
        img.save(PREVIEW / f"{stem}.jpg", quality=80)
        if n % 25 == 0:
            print(f"{n + 1}/{len(rows)} giu={kept} khung_soc={boxes_total} anh_nen={empty} bo={skipped}", flush=True)

    log_fh.close()
    (DATASET / "data.yaml").write_text(
        f"path: {DATASET.as_posix()}\ntrain: images/train\nval: images/val\ntest: images/test\n"
        f"names:\n" + "".join(f"  {i}: {c}\n" for i, c in enumerate(CLASSES)), encoding="utf-8")
    with (DATASET / "gan_nhan_report.csv").open("w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=["image", "split", "group", "boxes"])
        w.writeheader()
        w.writerows(report)
    print(f"Xong: xet {len(rows)} anh, giu {kept}. Khung soc: {boxes_total}. Anh nen khong soc: {empty}. Bo: {skipped}")


if __name__ == "__main__":
    main()
