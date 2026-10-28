# -*- coding: utf-8 -*-
"""Rebuild texture patch v5 with stroke-level ink removal.

`image_patch.clean_map_ink` selects ink by colour, which swallows whole terrain
patches on the maps and is rejected as "insufficient texture". This build keeps
the same manifest format but removes lettering with a black-hat / top-hat stroke
mask plus inpainting, so the underlying art stays intact.
"""
import hashlib
import json
import math
import shutil
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.stdout.reconfigure(encoding="utf-8")

proj = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass")
work = proj / "ImageText" / "work2"
styles_path = proj / "ImagePatch" / "texture_styles.json"
mapping_path = proj / "king-of-dragon-pass_image_text.mapping.json"
source_csv = proj / "king-of-dragon-pass_image_text.csv"
translated_csv = proj / "TranslateFiles/image-text/king-of-dragon-pass_image_text_translated.csv"
dest = proj / "ImagePatch" / "build-v5"
font_path = Path(r"C:\Windows\Fonts\simhei.ttf")

digest = lambda data: hashlib.sha256(data).hexdigest()


def layout(text, width, height, min_size=6, max_size=64):
    for size in range(max_size, min_size - 1, -1):
        font = ImageFont.truetype(str(font_path), size)
        lines = []
        for paragraph in text.split("\n"):
            line = ""
            for char in paragraph:
                if font.getlength(line + char) > width and line:
                    lines.append(line)
                    line = char
                else:
                    line += char
            lines.append(line)
        ascent, descent = font.getmetrics()
        step = ascent + descent
        if all(font.getlength(x) <= width for x in lines) and step * len(lines) <= height:
            return font, lines, step
    raise ValueError(f"translation does not fit box: {text[:20]}")


def stroke_mask(rgb, want_dark=True):
    gray = cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY)
    kernel = np.ones((5, 5), np.uint8)
    kind = cv2.MORPH_BLACKHAT if want_dark else cv2.MORPH_TOPHAT
    response = cv2.morphologyEx(gray, kind, kernel)
    threshold = max(24, int(response.mean() + response.std() * 1.5))
    mask = (response > threshold).astype(np.uint8) * 255
    mask = cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=1)
    return mask


def clean_region(image, rect, want_dark):
    """Inpaint only lettering strokes inside rect; return (image, cleaned_pixels)."""
    rgb = np.array(image.convert("RGB"))
    x1, y1, x2, y2 = rect
    crop = rgb[y1:y2, x1:x2]
    if crop.size == 0:
        return image, 0
    mask = stroke_mask(crop, want_dark)
    covered = int((mask > 0).sum())
    if covered == 0:
        return image, 0
    if covered >= mask.size * 0.85:
        return image, 0
    healed = cv2.inpaint(crop, mask, 3, cv2.INPAINT_TELEA)
    out = rgb.copy()
    out[y1:y2, x1:x2] = healed
    return Image.fromarray(out).convert("RGBA"), covered


def main():
    mapping = json.loads(mapping_path.read_text(encoding="utf-8"))
    styles = json.loads(styles_path.read_text(encoding="utf-8"))
    manifest = json.loads((work / "manifest.json").read_text(encoding="utf-8"))
    images = {img["image_id"]: img for img in manifest["images"]}
    target = [r[0] for r in __import__("csv").reader(translated_csv.open(encoding="utf-8"))]
    original = [r[0] for r in __import__("csv").reader(source_csv.open(encoding="utf-8"))]

    if dest.exists():
        shutil.rmtree(dest)
    (dest / "images").mkdir(parents=True)

    grouped = {}
    preserved = []
    for region in mapping["regions"]:
        if target[region["csv_row"] - 1] == original[region["csv_row"] - 1]:
            preserved.append({"image_id": region["image_id"], "box": region["box"],
                              "csv_row": region["csv_row"], "sources": region["sources"],
                              "reason": "translation preserves original text"})
            continue
        grouped.setdefault(region["image_id"], []).append(region)

    files, entries = [], []
    for iid, regions in grouped.items():
        src = work / "images" / f"{iid}.png"
        im = Image.open(src).convert("RGBA")
        style = styles.get(iid, {})
        ink_is_dark = not style.get("erase_light_ink", False)
        for region in regions:
            left, top, right, bottom = region["box"]
            rect = [math.floor(left), math.floor(top), math.ceil(right), math.ceil(bottom)]
            text = target[region["csv_row"] - 1]
            width, height = rect[2] - rect[0], rect[3] - rect[1]
            if style.get("clear_transparent_bg") or style.get("erase_text_ink") or style.get("erase_light_ink"):
                if style.get("clear_transparent_bg"):
                    im.paste((0, 0, 0, 0), tuple(rect))
                    cleaned = 0
                else:
                    im, cleaned = clean_region(im, rect, ink_is_dark)
            else:
                cleaned = 0
            limit = style.get("max_font_size", 96)
            if isinstance(limit, dict):
                limit = limit.get(str(region["csv_row"]), 96)
            font, lines, step = layout(text, width, height, max_size=limit)
            ink = tuple(style.get("text_rgba", (255, 255, 255, 255)))
            stroke = style.get("stroke_width", 1)
            overlay = Image.new("RGBA", (width, height), (0, 0, 0, 0))
            draw = ImageDraw.Draw(overlay)
            y = max(0, (height - step * len(lines)) // 2)
            for line in lines:
                x = max(0, (width - font.getlength(line)) / 2)
                draw.text((x, y), line, font=font, fill=ink, anchor="lt",
                          stroke_width=stroke, stroke_fill=(0, 0, 0, 200))
                y += step
            im.alpha_composite(overlay, (rect[0], rect[1]))
            entries.append({"image_id": iid, "box": region["box"], "text": text,
                            "csv_row": region["csv_row"], "sources": region["sources"],
                            "mask_rgba": [0, 0, 0, 0], "text_rgba": list(ink),
                            "rendering": {"font_size": font.size, "lines": lines,
                                          "background_mode": "stroke_inpaint" if cleaned else "text_only",
                                          "cleaned_pixels": cleaned, "stroke_width": stroke}})
        name = f"images/{iid}.png"
        path = dest / name
        im.save(path)
        files.append({"path": name, "sha256": digest(path.read_bytes()),
                      "image_id": iid, "sources": images[iid]["sources"]})
        print(f"{images[iid]['sources'][0]['path']}: {len(regions)} regions")

    payload = {
        "schema": 1, "status": "ready_for_engine_adapter", "mode": "texture", "installed": False,
        "source_sha256": digest(source_csv.read_bytes()),
        "translation_sha256": digest(translated_csv.read_bytes()),
        "mapping_sha256": digest(mapping_path.read_bytes()),
        "font": {"name": font_path.name, "sha256": digest(font_path.read_bytes()),
                 "variable_weight_requested": 400},
        "files": files, "entries": entries, "preserved_regions": preserved,
        "visual_review": {"status": "pending", "policy": "preserve_art_v2",
                          "checks": ["original_art_visibility", "no_old_text_ghosting",
                                     "style_and_legibility", "font_size_and_edges"]},
        "note": "Stroke-level ink removal; art preserved. Engine import still required.",
    }
    (dest / "IMAGE_PATCH_MANIFEST.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"built {len(files)} files, {len(entries)} entries -> {dest}")


if __name__ == "__main__":
    main()
