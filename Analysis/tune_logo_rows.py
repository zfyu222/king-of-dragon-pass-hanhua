# -*- coding: utf-8 -*-
"""Give the two stacked Logo rows separate font limits so the glyphs never collide."""
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
p = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass\ImagePatch\texture_styles.json")
styles = json.loads(p.read_text(encoding="utf-8"))

# csv_row 28 = KING OF (upper small row), 29 = DRAGON PASS (lower big row)
logo_rows = {
    "d6fae21d780e32becd3f55ef08a67c95fcca4a64e46cdb5d06e19c2e171887a2": (25, 44),
    "43584de2921553cf1dcdd273839550f8d522b8458d99e4e735f1bb0b90c1e466": (50, 88),
    "98fc4e10620d4464a981299048be857b5fc3e0c06fb164b023c7ddccd794e04a": (51, 90),
    "693210c955d757c2ee504a173b15e8f00358e0c9075c6b37ace86f250cb84217": (102, 180),
}
for key, (upper, lower) in logo_rows.items():
    styles[key]["max_font_size"] = {"28": upper, "29": lower}

# slightly wider blur for the busiest map variants
blur_bump = {
    "59f050c50a106f99034b711399389ab1f5e72ef968f4f98632d3fc75274d9766": 6,
    "7e3ca9ba51f0e425649ce9e17b21564eb2e0b2fb672852a4c50cf5a79de957c9": 7,
    "e7a6e3f465b99cd48575aa3ac16e37b36d908f3a9c24c603b615da992336f7c3": 8,
    "e71c97a6d2217c18afa2dd12a270c67aa095d0b8c4e291fd402697f57e5c34c9": 8,
}
for key, radius in blur_bump.items():
    styles[key]["blur_radius"] = radius

p.write_text(json.dumps(styles, ensure_ascii=False, indent=2), encoding="utf-8")
print("logo rows:", len(logo_rows), "blur bumped:", len(blur_bump))
