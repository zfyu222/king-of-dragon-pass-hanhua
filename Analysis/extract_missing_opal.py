"""Lossless resource-field audit; arbitrary Opal operand bytes include NUL.

Never overwrite the approved deduplicated corpus or its workflow state.
"""
import csv
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def escape_plain(raw):
    text = raw.decode('utf-8')
    if '\0' in text:
        raise ValueError('NUL outside an Opal operand')
    text = text.replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t')
    return re.sub(r'[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]', lambda m:'\\x%02X'%ord(m[0]), text)


def decode(raw):
    chunks, literal, pos, branch = [], 0, 0, False
    while pos < len(raw):
        if raw[pos] == 1:
            if pos+2 >= len(raw):raise ValueError('truncated variable')
            chunks.append(escape_plain(raw[literal:pos]))
            chunks.append(''.join('\\x%02X'%b for b in raw[pos:pos+3]))
            pos += 3; literal = pos
        elif raw[pos] == 2:
            if branch:
                branch=False; pos+=1
            else:
                if pos+1>=len(raw) or not 1<=raw[pos+1]<=31:raise ValueError('bad random count')
                # A random count of 1 is data, not the start of an 01 command.
                pos+=2; branch=True
        else:
            pos+=1
    if branch:raise ValueError('unclosed random branch')
    chunks.append(escape_plain(raw[literal:]))
    return ''.join(chunks)


def extract():
    data=(ROOT/'Game/KoDp/scenes.dat').read_bytes()
    count,table_end=struct.unpack_from('<HH',data,4)
    assert table_end==8+count*4
    known=set()
    with (ROOT/'king-of-dragon-pass_text_catalog.tsv').open(encoding='utf-8-sig',newline='') as f:
        for r in csv.DictReader(f,delimiter='\t'):
            if r['kind']=='scenes.dat':known.add(int(r['locator'].split('offset=')[1].split(';')[0],16))
    missing=[];all_strings=0;recovered_nul=0
    for scene in range(1,count+1):
        block=struct.unpack_from('<I',data,4+scene*4)[0]
        end=block+2+struct.unpack_from('<H',data,block)[0]*2
        assert end<=len(data)
        covered=-1
        for offset in range(block+2,end-3,2):
            if offset<covered:continue
            length=struct.unpack_from('<H',data,offset)[0];finish=offset+2+length
            if not 1<=length<=8192 or finish>=end or data[finish]!=0:continue
            raw=data[offset+2:finish]
            try:source=decode(raw)
            except (ValueError,UnicodeDecodeError):continue
            visible=re.sub(r'\\x01(?:\\x[0-9A-F]{2}){2}|\\x[0-9A-F]{2}|\\[nrt]', '',source)
            if not re.search('[A-Za-z]{2}',visible):continue
            covered=finish+1+(length%2==0);all_strings+=1
            if offset in known:continue
            missing.append(dict(scene=scene,offset=offset,bytes=length,source=source,source_bytes_sha256=sha(raw),nul_operands=b'\0' in raw))
            recovered_nul+=b'\0' in raw
    result=dict(resource='KoDp/scenes.dat',resource_sha256=sha(data),scenes=count,
                accepted_strings=all_strings,known_extracted_fields=len(known),missing_fields=len(missing),
                missing_with_nul_operands=recovered_nul,records=missing,approved_corpus_modified=False)
    (ROOT/'Analysis/opal_missing_fields_v2.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='records'}))


if __name__=='__main__':extract()
