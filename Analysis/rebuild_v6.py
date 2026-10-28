# -*- coding: utf-8 -*-
"""Texture patch v6: bigger map labels, cleaner stroke removal, redrawn logos.

Map labels keep the art but switch to a stronger ink mask and a sane font cap.
Logos are handled separately: the lettering sits on flat black, so bright pixels
inside the text band are cleared to black and the Chinese title is re-set in a
serif face instead of being inked over the surviving Latin glyphs.
"""
import csv
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
dest = proj / "ImagePatch" / "build-v11"

BODY_FONT = Path(r"C:\Windows\Fonts\simhei.ttf")
TITLE_FONT = Path(r"C:\Windows\Fonts\simsun.ttc")

LOGO_PATHS = {"Menu/Logo.png", "Menu/Logo@2x.png", "Menu/Logo~ipad.png", "Menu/Logo~ipad@2x.png"}
MAP_PATHS = {"Lore/background-map.png", "Lore/background-map1024.png",
             "Lore/background-map1024@2x.png", "Lore/background-map@2x.png"}
# Flat caption plates: the caption is drawn by the engine at runtime, the PNG has no
# baked lettering. Inking a label here would duplicate or misplace the runtime text.
SKIP_PATHS = {"M/RecruitCaption.png", "M/DismissCaption.png"}

digest = lambda data: hashlib.sha256(data).hexdigest()


def layout(text, width, height, font_path, min_size=6, max_size=64, stroke=0):
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
        widest = max(font.getlength(x) for x in lines) + stroke * 2
        if widest <= width and step * len(lines) <= height:
            return font, lines, step
    raise ValueError(f"text does not fit: {text[:24]}")


def strong_mask(rgb, want_dark=True, sensitivity=1.0):
    gray = cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY)
    kernel = np.ones((5, 5), np.uint8)
    kind = cv2.MORPH_BLACKHAT if want_dark else cv2.MORPH_TOPHAT
    response = cv2.morphologyEx(gray, kind, kernel)
    threshold = max(14, int(response.mean() + response.std() * sensitivity))
    mask = (response > threshold).astype(np.uint8) * 255
    mask = cv2.dilate(mask, np.ones((3, 3), np.uint8), iterations=1)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))
    return mask


def clean_strokes(image, rect, want_dark=True, rounds=2, sensitivity=1.0):
    rgb = np.array(image.convert("RGB"))
    x1, y1, x2, y2 = rect
    crop = rgb[y1:y2, x1:x2]
    if crop.size == 0:
        return image, 0
    total = 0
    current = crop
    for _ in range(rounds):
        mask = strong_mask(current, want_dark, sensitivity)
        covered = int((mask > 0).sum())
        if covered == 0 or covered >= mask.size * 0.85:
            break
        current = cv2.inpaint(current, mask, 3, cv2.INPAINT_TELEA)
        total += covered
    if total == 0:
        return image, 0
    out = rgb.copy()
    out[y1:y2, x1:x2] = current
    return Image.fromarray(out).convert("RGBA"), total


def clear_bright(image, rect, threshold=140):
    """Logo helper: the art is white lettering on flat black, so erase by luminance."""
    rgba = np.array(image)
    x1, y1, x2, y2 = rect
    crop = rgba[y1:y2, x1:x2]
    if crop.size == 0:
        return image, 0
    rgb = crop[:, :, :3].astype(np.int16)
    bright = ((rgb.max(axis=2) > threshold) & (crop[:, :, 3] > 40)).astype(np.uint8) * 255
    if not bright.any():
        return image, 0
    # Grow the mask so anti-aliased edges of the Latin glyphs go too; the logo
    # background is flat black, so filling the grown mask is lossless there.
    bright = cv2.dilate(bright, np.ones((3, 3), np.uint8), iterations=2)
    crop[bright > 0] = (0, 0, 0, 255)
    rgba[y1:y2, x1:x2] = crop
    return Image.fromarray(rgba), int(bright.sum())


def main():
    mapping = json.loads(mapping_path.read_text(encoding="utf-8"))
    styles = json.loads(styles_path.read_text(encoding="utf-8"))
    manifest = json.loads((work / "manifest.json").read_text(encoding="utf-8"))
    images = {img["image_id"]: img for img in manifest["images"]}
    target = [r[0] for r in csv.reader(translated_csv.open(encoding="utf-8"))]
    original = [r[0] for r in csv.reader(source_csv.open(encoding="utf-8"))]

    if dest.exists():
        shutil.rmtree(dest)
    (dest / "images").mkdir(parents=True)

    grouped, preserved = {}, []
    for region in mapping["regions"]:
        path = region["sources"][0]["path"]
        if path in SKIP_PATHS:
            preserved.append({"image_id": region["image_id"], "box": region["box"],
                              "csv_row": region["csv_row"], "sources": region["sources"],
                              "reason": "flat caption plate; caption is drawn by the engine at runtime"})
            continue
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
        path = images[iid]["sources"][0]["path"]
        is_logo = path in LOGO_PATHS
        is_map = path in MAP_PATHS
        is_arch = path == "M/MapArch.png"

        for region in regions:
            left, top, right, bottom = region["box"]
            rect = [math.floor(left), math.floor(top), math.ceil(right), math.ceil(bottom)]
            text = target[region["csv_row"] - 1]
            width, height = rect[2] - rect[0], rect[3] - rect[1]

            if is_logo:
                im, cleaned = clear_bright(im, rect)
                font_file = TITLE_FONT
                stroke = 2
                ink = (255, 255, 255, 255)
                limit = 220
            elif style.get("clear_transparent_bg"):
                im.paste((0, 0, 0, 0), tuple(rect))
                cleaned, font_file, stroke = 0, BODY_FONT, style.get("stroke_width", 1)
                ink = tuple(style.get("text_rgba", (255, 255, 255, 255)))
                limit = style.get("max_font_size", 96)
            else:
                want_dark = not style.get("erase_light_ink", False)
                sensitivity = 0.75 if is_map else 0.9 if is_arch else 1.2
                im, cleaned = clean_strokes(im, rect, want_dark, rounds=3, sensitivity=sensitivity)
                font_file = BODY_FONT
                stroke = style.get("stroke_width", 1)
                ink = tuple(style.get("text_rgba", (255, 255, 255, 255)))
                limit = style.get("max_font_size", 96)
                if is_map:
                    ink = (26, 34, 20, 255)
                    stroke = 1
                if is_arch:
                    ink = (92, 60, 26, 255)
                    stroke = 2
                    limit = max(limit, 26)

            if isinstance(limit, dict):
                limit = limit.get(str(region["csv_row"]), 96)
            if is_map:
                limit = int(limit * 1.45)

            if is_logo:
                # Fill the original lettering band instead of laying out like body text.
                size = max(8, min(int(height * 0.98), int(width * 0.98 / max(1, len(text)))))
                font = ImageFont.truetype(str(font_file), size)
                lines, step = [text], int(size * 1.0)
                stroke = max(2, int(size * 0.025))
            else:
                font, lines, step = layout(text, width, height, font_file, max_size=limit, stroke=stroke)

            if is_logo:
                edge = tuple(ink)          # self-coloured stroke thickens the serif face
            elif is_map or is_arch:
                edge = (238, 231, 203, 220)
            else:
                edge = (0, 0, 0, 230)
            overlay = Image.new("RGBA", (width, height), (0, 0, 0, 0))
            draw = ImageDraw.Draw(overlay)
            y = max(0, (height - step * len(lines)) // 2)
            for line in lines:
                x = max(0, (width - font.getlength(line) - stroke * 2) / 2)
                draw.text((x, y), line, font=font, fill=ink, anchor="lt",
                          stroke_width=stroke, stroke_fill=edge)
                y += step
            im.alpha_composite(overlay, (rect[0], rect[1]))
            entries.append({"image_id": iid, "box": region["box"], "text": text,
                            "csv_row": region["csv_row"], "sources": region["sources"],
                            "mask_rgba": [0, 0, 0, 0], "text_rgba": list(ink),
                            "rendering": {"font": font_file.name, "font_size": font.size,
                                          "lines": lines, "stroke_width": stroke,
                                          "cleaned_pixels": cleaned,
                                          "background_mode": "logo_redraw" if is_logo
                                          else "stroke_inpaint" if cleaned else "text_only"}})
        name = f"images/{iid}.png"
        out_path = dest / name
        im.save(out_path)
        files.append({"path": name, "sha256": digest(out_path.read_bytes()),
                      "image_id": iid, "sources": images[iid]["sources"]})
        print(f"{path}: {len(regions)} regions, logo={is_logo}")

    payload = {
        "schema": 1, "status": "ready_for_engine_adapter", "mode": "texture", "installed": False,
        "source_sha256": digest(source_csv.read_bytes()),
        "translation_sha256": digest(translated_csv.read_bytes()),
        "mapping_sha256": digest(mapping_path.read_bytes()),
        "font": {"body": BODY_FONT.name, "title": TITLE_FONT.name,
                 "body_sha256": digest(BODY_FONT.read_bytes()),
                 "title_sha256": digest(TITLE_FONT.read_bytes())},
        "files": files, "entries": entries, "preserved_regions": preserved,
        "visual_review": {"status": "pending", "policy": "preserve_art_v3",
                          "checks": ["original_art_visibility", "no_old_text_ghosting",
                                     "style_and_legibility", "font_size_and_edges"]},
        "note": "Stroke-level ink removal plus redrawn logo lettering; engine import still required.",
    }
    (dest / "IMAGE_PATCH_MANIFEST.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"built {len(files)} files, {len(entries)} entries")


if __name__ == "__main__":
    main()
