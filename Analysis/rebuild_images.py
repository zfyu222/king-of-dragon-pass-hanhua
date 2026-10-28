# -*- coding: utf-8 -*-
"""Rebuild the texture patch with corrected boxes and local ink cleanup.

Corrections applied:
  * M/ClanButton.png box no longer covers the painted icon.
  * M/MapArch.png box moved onto the rotated "Dragon Pass" lettering.
  * Map / placeholder / logo styles stop painting an opaque or blurred slab:
    ink is cleaned locally with inpainting and the mask stays transparent.
"""
import json
import shutil
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, r"D:\StaticHanHua\Tools\image_text")
import image_patch  # noqa: E402

proj = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass")
out = proj / "ImageText" / "work2"
mapping_path = proj / "king-of-dragon-pass_image_text.mapping.json"
styles_path = proj / "ImagePatch" / "texture_styles.json"
dest = proj / "ImagePatch" / "build-v5"
font = Path(r"C:\Windows\Fonts\simhei.ttf")

# 1. corrected boxes (measured earlier from the agent pack)
BOX_FIXES = {
    "M/ClanButton.png": [49.0, 11.0, 92.0, 28.0],
    "M/MapArch.png": [46.0, 15.0, 87.0, 51.0],
}

shutil.copy2(mapping_path, mapping_path.with_suffix(".json.bak_before_boxfix"))
shutil.copy2(styles_path, styles_path.with_suffix(".json.bak_before_boxfix"))

mapping = json.loads(mapping_path.read_text(encoding="utf-8"))
changed = 0
for region in mapping["regions"]:
    path = region["sources"][0]["path"]
    if path in BOX_FIXES:
        print(f"box  {path}: {region['box']} -> {BOX_FIXES[path]}")
        region["box"] = BOX_FIXES[path]
        changed += 1
mapping_path.write_text(json.dumps(mapping, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"boxes changed: {changed}")

# 2. per-image styles, keyed by source path -> image_id
manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
by_path = {img["sources"][0]["path"]: img["image_id"] for img in manifest["images"]}

styles = json.loads(styles_path.read_text(encoding="utf-8"))
MAPS = ["Lore/background-map.png", "Lore/background-map1024.png",
        "Lore/background-map1024@2x.png", "Lore/background-map@2x.png"]
PLACEHOLDERS = ["Scenes/scene226.jpg", "Scenes~ipad/scene226.jpg"]
LOGOS = ["Menu/Logo.png", "Menu/Logo@2x.png", "Menu/Logo~ipad.png", "Menu/Logo~ipad@2x.png"]


def style_for(path):
    iid = by_path.get(path)
    if iid is None:
        return None
    return styles.setdefault(iid, {})


for path in MAPS:
    st = style_for(path)
    if st is None:
        continue
    st["mask_rgba"] = [0, 0, 0, 0]
    st["blur_radius"] = 0
    st["erase_text_ink"] = True
    st.pop("erase_light_ink", None)

for path in PLACEHOLDERS:
    st = style_for(path)
    if st is None:
        continue
    st["mask_rgba"] = [0, 0, 0, 0]
    st["erase_text_ink"] = True
    st["stroke_width"] = 0

for path in LOGOS:
    st = style_for(path)
    if st is None:
        continue
    st["mask_rgba"] = [0, 0, 0, 0]
    st["clear_transparent_bg"] = False
    st["erase_light_ink"] = True
    st["stroke_width"] = 1

clan = style_for("M/ClanButton.png")
if clan is not None:
    clan["mask_rgba"] = [0, 0, 0, 0]
    clan["erase_text_ink"] = True
    clan["max_font_size"] = 22

arch = style_for("M/MapArch.png")
if arch is not None:
    arch["mask_rgba"] = [0, 0, 0, 0]
    arch["blur_radius"] = 0
    arch["erase_light_ink"] = True

styles_path.write_text(json.dumps(styles, ensure_ascii=False, indent=2), encoding="utf-8")
print("styles updated")

# 3. build
if dest.exists():
    shutil.rmtree(dest)
payload = image_patch.build(
    out=str(out),
    source=str(proj / "king-of-dragon-pass_image_text.csv"),
    translated=str(proj / "TranslateFiles/image-text/king-of-dragon-pass_image_text_translated.csv"),
    validation=str(proj / "TranslateFiles/image-text/TranslateFiles/reports/final_validation.json"),
    font_path=str(font),
    destination=str(dest),
    mode="texture",
    mask_alpha=64,
    text_rgba=(255, 255, 255, 255),
    texture_styles=styles,
)
print("built", len(payload["files"]), "files", len(payload["entries"]), "entries")
for e in payload["entries"][:3]:
    print(" ", e["sources"][0]["path"], e["rendering"]["background_mode"])
