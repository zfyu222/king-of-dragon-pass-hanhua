"""Audit skipped Opal strings without overwriting the approved source corpus.

Opal 01 commands carry two arbitrary operand bytes, not UTF-8 text.
Escape these operands before decoding the surrounding UTF-8 narrative.
"""
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def decode_opal(raw):
    chunks, start, cursor = [], 0, 0
    while cursor < len(raw):
        if raw[cursor] == 1:
            if cursor + 2 >= len(raw):
                raise ValueError('truncated Opal command')
            chunks.append(raw[start:cursor].decode('utf-8'))
            chunks.append(''.join('\\x%02X' % b for b in raw[cursor:cursor+3]))
            cursor += 3
            start = cursor
        else:
            cursor += 1
    chunks.append(raw[start:].decode('utf-8'))
    return ''.join(chunks)


def audit():
    path = ROOT / 'Game/KoDp/scenes.dat'
    data = path.read_bytes()
    count, table_end = struct.unpack_from('<HH', data, 4)
    assert table_end == 8 + count * 4
    missing = []
    for scene in range(1, count + 1):
        block = struct.unpack_from('<I', data, 4 + scene * 4)[0]
        end = block + 2 + struct.unpack_from('<H', data, block)[0] * 2
        assert end <= len(data)
        covered = -1
        for offset in range(block + 2, end - 3, 2):
            length = struct.unpack_from('<H', data, offset)[0]
            finish = offset + 2 + length
            if not 1 <= length <= 8192 or finish >= end or data[finish] != 0 or offset < covered:
                continue
            raw = data[offset+2:finish]
            if b'\0' in raw:
                continue
            try:
                decoded = raw.decode('utf-8')
                if any(c.isalpha() for c in decoded):
                    covered = finish + 1 + (length % 2 == 0)
                continue
            except UnicodeDecodeError:
                pass
            try:
                value = decode_opal(raw)
            except (ValueError, UnicodeDecodeError):
                continue
            visible = re.sub(r'\\x[0-9A-F]{2}', '', value)
            if not any(c.isalpha() for c in visible):
                continue
            covered = finish + 1 + (length % 2 == 0)
            missing.append(dict(scene=scene, offset=offset, bytes=length,
                                source=value, source_bytes_sha256=hashlib.sha256(raw).hexdigest()))
    result = dict(resource='KoDp/scenes.dat', resource_sha256=hashlib.sha256(data).hexdigest(),
                  candidate_occurrences=len(missing), occurrences=missing,
                  status='candidates_require_confirmation_and_translation', approved_corpus_modified=False)
    out = ROOT / 'Analysis/opal_utf8_omissions.json'
    out.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k != 'occurrences'}))


if __name__ == '__main__':
    audit()
