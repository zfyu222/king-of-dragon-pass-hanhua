# -*- coding: utf-8 -*-
"""Scan UI sprites with RapidOCR to find lettering baked into textures."""
import argparse
import json
import sys
import time
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

from rapidocr_onnxruntime import RapidOCR

PROJECT = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass")
TEX = PROJECT / "Game" / "KoDp" / "textures"
DEFAULT_DIRS = ["", "Common", "Controls", "Menu", "M", "T", "Tula", "Battle", "EndGame", "EndGame~ipad", "Intro", "Intro_ipad", "Lore", "lashki", "pl/pashki", "SacredTime"]
EXTS = {".png", ".jpg", ".jpeg", ".tga"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dirs", nargs="*", default=None)
    ap.add_argument("--out", default=str(PROJECT / "Analysis" / "ocr_result.json"))
    ap.add_argument("--min-score", type=float, default=0.55)
    args = ap.parse_args()

    dirs = args.dirs if args.dirs else ["", "Common", "Controls", "Menu", "M", "T", "Tula", "Battle", "EndGame", "EndGame~ipad", "Intro", "Intro_ipad", "pl/pashki", "SacredTime"]
    engine = RapidOCR()
    files = []
    for d in dirs:
        base = TEX / d if d else TEX
        if not base.exists():
            continue
        files.extend(sorted(p for p in base.iterdir() if p.is_file() and p.suffix.lower() in EXTS))

    results = []
    t0 = time.time()
    for n, path in enumerate(files, 1):
        try:
            res, _ = engine(str(path))
        except Exception as exc:  # noqa: BLE001
            print("ERR", path.name, exc)
            continue
        hits = []
        for box, text, score in (res or []):
            text = text.strip()
            if not text or score < args.min_score:
                continue
            xs = [p[0] for p in box]
            ys = [p[1] for p in box]
            hits.append(dict(text=text, score=round(float(score), 3),
                             box=[int(min(xs)), int(min(ys)), int(max(xs)), int(max(ys))]))
        if hits:
            results.append(dict(file=str(path.relative_to(PROJECT)), hits=hits))
        if n % 25 == 0:
            print(f"{n}/{len(files)} {time.time()-t0:.0f}s hits={len(results)}", flush=True)

    Path(args.out).write_text(json.dumps(results, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"done files={len(files)} with_text={len(results)} elapsed={time.time()-t0:.0f}s -> {args.out}")


if __name__ == "__main__":
    main()
