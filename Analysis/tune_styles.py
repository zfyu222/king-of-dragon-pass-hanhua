# -*- coding: utf-8 -*-
"""Replace colour-based map ink removal with a bounded blur, which fits busy terrain art."""
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
p = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass\ImagePatch\texture_styles.json")
styles = json.loads(p.read_text(encoding="utf-8"))

map_blur = {
    "59f050c50a106f99034b711399389ab1f5e72ef968f4f98632d3fc75274d9766": 4,
    "7e3ca9ba51f0e425649ce9e17b21564eb2e0b2fb672852a4c50cf5a79de957c9": 5,
    "e7a6e3f465b99cd48575aa3ac16e37b36d908f3a9c24c603b615da992336f7c3": 6,
    "e71c97a6d2217c18afa2dd12a270c67aa095d0b8c4e291fd402697f57e5c34c9": 7,
    "42505a0a03254b513d1d6a74bb6f3bce45ff84595e5f1a47d2f1dcbc0f6d1414": 3,
}
for key, radius in map_blur.items():
    st = styles[key]
    st.pop("erase_text_ink", None)
    st.pop("erase_light_ink", None)
    st["mask_rgba"] = [0, 0, 0, 0]
    st["blur_radius"] = radius
p.write_text(json.dumps(styles, ensure_ascii=False, indent=2), encoding="utf-8")
print("tuned", len(map_blur), "styles")
