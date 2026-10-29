"""Bind reviewed runtime sentences to their real Opal component fields."""
import csv, hashlib, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
    project=ROOT/'TranslateFiles/runtime-composites-20261030'
    source=project/'feedback_text.csv'; raw=(ROOT/'Game/KoDp/scenes.dat').read_bytes()
    rows=[r[0] for r in csv.reader(source.open(encoding='utf-8-sig',newline=''))]
    def binding(offset):
        length=int.from_bytes(raw[offset:offset+2],'little'); field=raw[offset+2:offset+2+length]
        assert raw[offset+2+length]==0 and length<4096
        return dict(resource='KoDp/scenes.dat',offset=offset,bytes=length,source_bytes_sha256=sha(field))
    medical=[27608,27704,27728,27744,27760,27824]
    records=[]
    for ordinal,text in enumerate(rows,1):
        if ordinal<=18:
            offsets=medical+[o+2212 for o in medical]
            transform='Complete displayed medical sentence from ordered Opal fields; select one prefix random branch, singular/plural warrior field and final healed/patched up/bandaged or dying-only branch. Two numeric captures retain the original order; all alternative component fields are bound, not concatenated as CSV neighbours.'
        elif ordinal==19:
            offsets=[32576,32596]
            transform='Observed saga sentence: We stole <count> cows + from the <clan>. Numeric then clan capture, as shown in screenshot 37898174.'
        else:
            offsets=[34886]
            transform='Observed saga sentence: clan and Sea season; season is retained as an approved full captured phrase. Source resource is the exact Sea-season refusal field, screenshot 37898174.'
        records.append(dict(ordinal=ordinal,source_sha256=sha(text.encode()),bindings=[binding(o) for o in offsets],transform=transform))
    out=dict(schema='runtime-template-occurrences.v1',source_csv_sha256=sha(source.read_bytes()),resource_sha256=sha(raw),records=records)
    (project/'feedback_text.mapping.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
    print(f'Bound {len(records)} complete templates to original component hashes')
if __name__=='__main__':main()
