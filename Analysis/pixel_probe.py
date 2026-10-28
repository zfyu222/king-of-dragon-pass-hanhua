# -*- coding: utf-8 -*-
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
from PIL import Image

base = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass\ImageText\work\images")
targets = {
    "693210c955d7": "Logo~ipad@2x",
    "43584de29215": "Logo@2x",
    "f9f2ca4d8c9f": "ClanButton",
    "fb7062597c1b": "DismissCaption",
    "31e999d0b719": "RecruitCaption",
    "17473c8ce745": "scene226",
    "42505a0a0325": "MapArch",
}
for key, name in targets.items():
    p = next(base.glob(key + "*.png"))
    im = Image.open(p).convert("RGBA")
    w, h = im.size
    pts = {"corner": (2, 2), "center": (w // 2, h // 2), "edge": (w // 2, h - 3), "left": (3, h // 2)}
    print(f"{name:20s} {w}x{h} alpha_mode={im.getchannel('A').getextrema()}")
    for label, xy in pts.items():
        print(f"   {label:8s} {xy} = {im.getpixel(xy)}")
