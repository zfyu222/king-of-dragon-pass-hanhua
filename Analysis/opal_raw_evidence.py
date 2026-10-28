"""Read original scene bytes using catalog offsets (offset includes u16 length)."""
import csv
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
root = Path(__file__).resolve().parents[1]
catalog = csv.DictReader((root / 'king-of-dragon-pass_text_catalog.tsv').open(encoding='utf-8-sig'), delimiter='\t')
evidence = []
for row in catalog:
    if row['corpus_index'] != '15010':
        continue
    path = (root / 'Game' / row['source']).resolve()
    data = path.read_bytes()
    offset = int(row['locator'].split('offset=')[1].split(';')[0], 16)
    length = struct.unpack_from('<H', data, offset)[0]
    raw = data[offset + 2:offset + 2 + length]
    evidence.append({'source_path': str(path), 'sha256': hashlib.sha256(data).hexdigest(),
        'record_id': 'R0015011_3ed5cd1f0c', 'length_field_offset': hex(offset),
        'text_offset': hex(offset + 2), 'length': length, 'hex': raw.hex(' '),
        'utf8': raw.decode('utf-8'),
        'observed': 'The sequence is 01 c3 81 2c; U+00C1 is encoded as c3 81. This alone does not prove the engine command grammar.'})
out = root / 'Analysis/out/opal_raw_evidence_20261028.json'
out.parent.mkdir(exist_ok=True)
out.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
print(out)
print(json.dumps(evidence, ensure_ascii=False, indent=2))
