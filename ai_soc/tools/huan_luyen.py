#!/usr/bin/env python3
"""Huan luyen YOLO26n (yolo26n.pt) nhan dien SOC tren dataset/ da gan nhan tu dong, roi xuat:
  ai_soc/models/soc_yolo26n.pt    (dung tren may tinh / Python)
  ai_soc/models/soc_yolo26n.onnx  (dung cho web / dien thoai)

Cach lam giong ai_camera/tools/train_yolo.py cua du an Huynh Thai Nguyen.

  python huan_luyen.py --epochs 50
Can GPU CUDA de nhanh (RTX 4050 6GB: batch 16, imgsz 640).
"""

import argparse
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "dataset" / "data.yaml"
MODELS = ROOT / "models"
NAME = "soc_yolo26n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--epochs", type=int, default=50)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--imgsz", type=int, default=640)
    ap.add_argument("--weights", default="yolo26n.pt")
    args = ap.parse_args()

    import torch
    from ultralytics import YOLO

    device = 0 if torch.cuda.is_available() else "cpu"
    model = YOLO(args.weights)
    model.train(data=str(DATA), epochs=args.epochs, imgsz=args.imgsz, batch=args.batch, device=device,
                workers=2, patience=25, project=str(ROOT / "runs"), name=NAME, exist_ok=True,
                cos_lr=True, close_mosaic=10, seed=17, plots=True, verbose=False)
    best = ROOT / "runs" / NAME / "weights" / "best.pt"
    trained = YOLO(str(best))
    metrics = {}
    for split in ("val", "test"):
        m = trained.val(data=str(DATA), split=split, imgsz=args.imgsz, device=device, verbose=False,
                        project=str(ROOT / "runs"), name=f"eval_{split}", exist_ok=True)
        metrics[split] = {"mAP50": round(float(m.box.map50), 4), "mAP50-95": round(float(m.box.map), 4),
                          "precision": round(float(m.box.mp), 4), "recall": round(float(m.box.mr), 4)}
    (ROOT / "runs" / NAME / "metrics_summary.json").write_text(
        json.dumps(metrics, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(metrics, indent=2, ensure_ascii=False))
    MODELS.mkdir(parents=True, exist_ok=True)
    shutil.copy2(best, MODELS / f"{NAME}.pt")
    onnx = YOLO(str(best)).export(format="onnx", imgsz=args.imgsz, simplify=True)
    shutil.copy2(onnx, MODELS / f"{NAME}.onnx")
    print("Da luu", MODELS / f"{NAME}.pt", "va", MODELS / f"{NAME}.onnx")


if __name__ == "__main__":
    main()
