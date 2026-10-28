# -*- coding: utf-8 -*-
"""Extract PNG payloads from Marmalade group.bin archives and map them to the group index."""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

GAME = Path(__file__).resolve().parents[1] / "Game"
OUT = Path(__file__).resolve().parent / "images"
OUT.mkdir(parents=True, exist_ok=True)

PNG_SIG = b"\x89PNG\r\n\x1a\n"
PNG_END = b"IEND\xaeB`\x82"


def index_names(group_index: Path, ext_list):
    text = group_index.read_bytes().decode("latin-1")
    names = []
    for line in re.split(r"[\r\n]+", text):
        s = line.strip().rstrip("{").strip()
        s = re.sub(r"\s*\{.*$", "", s).strip()
        if any(s.lower().endswith(e) for e in ext_list):
            names.append(s)
    return names


def carve(data: bytes, sig: bytes, end: bytes):
    out = []
    pos = 0
    while True:
        i = data.find(sig, pos)
        if i < 0:
            break
        j = data.find(end, i)
        if j < 0:
            break
        out.append((i, j + len(end)))
        pos = j + len(end)
    return out


def main():
    report = []
    for idx_name, bin_name, exts in (
        ("KoDP_res.group", "KoDP_res.group.bin", (".png",)),
        ("KoDP.group", "KoDP.group.bin", (".png", ".tga")),
    ):
        index_path = GAME / idx_name
        bin_path = GAME / bin_name
        if not bin_path.exists():
            continue
        data = bin_path.read_bytes()
        names = index_names(index_path, exts)
        hits = carve(data, PNG_SIG, PNG_END)
        print(f"{bin_name}: png_payloads={len(hits)} index_png_names={len(names)}")
        for n, (start, stop) in enumerate(hits):
            payload = data[start:stop]
            name = names[n] if n < len(names) else f"unknown_{n}"
            safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", Path(name).name)
            rel = f"{bin_name}_{n:03d}_{safe}"
            target = OUT / rel
            target.write_bytes(payload)
            # PNG dimensions
            w = h = None
            if payload[:8] == PNG_SIG:
                w, h = struct.unpack(">II", payload[16:24])
            report.append(dict(bin=bin_name, index=n, name=name, file=rel, offset=start,
                               size=len(payload), width=w, height=h,
                               sha256=hashlib.sha256(payload).hexdigest()))
    (OUT.parent / "images_index.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    print("wrote", len(report), "images ->", OUT)


if __name__ == "__main__":
    main()
