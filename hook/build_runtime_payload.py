"""Build only the deployed native-hook dictionary and whitelisted HTML pages.

Requires the approved export; checks HTML against the extraction before replacing.
Never edits the game, old releases, scenes.dat, UI, or group archives.
"""
import argparse
import csv
import hashlib
import html
import json
import re
import itertools
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TAG = re.compile(r'(<(title|h[1-6]|p|li|td|th)\b[^>]*>)([\s\S]*?)(</\2>)', re.I)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_csv(path):
    with path.open(encoding='utf-8-sig', newline='') as f:
        rows = list(csv.reader(f))
    if any(len(r) != 1 for r in rows):
        raise ValueError('CSV must be single-column')
    return [r[0] for r in rows]


def unescape(text):
    return re.sub(r'\\(x[0-9a-fA-F]{2}|[\\nrt])',
                  lambda m: chr(int(m[1][1:], 16)) if m[1].startswith('x') else
                  {'\\': '\\', 'n': '\n', 'r': '\r', 't': '\t'}[m[1]], text)


def normalize(text):
    text = re.sub(r'<br\s*/?>', '\n', text, flags=re.I)
    text = html.unescape(re.sub(r'<[^>]+>', '', text))
    return re.sub(r'\s{2,}', ' ', ' '.join(x.strip() for x in text.splitlines() if x.strip())).strip()


def expanded_pairs(source, target):
    def branches(raw):
        found=[];cursor=0
        while cursor<len(raw):
            if raw[cursor]==1:
                if cursor+3>len(raw):return []
                cursor+=3;continue
            if raw[cursor]!=2:cursor+=1;continue
            start=cursor
            if cursor+1>=len(raw):return []
            count=raw[cursor+1];cursor+=2;part=cursor;options=[]
            while cursor<len(raw):
                if raw[cursor]==1:
                    if cursor+3>len(raw):return []
                    cursor+=3;continue
                if raw[cursor] in (2,47):
                    options.append(raw[part:cursor]);part=cursor+1
                    if raw[cursor]==2:break
                cursor+=1
            if cursor>=len(raw) or count!=len(options):return []
            found.append((start,cursor+1,options));cursor+=1
        return found
    a,b=branches(source),branches(target)
    if not a or len(a) != len(b): return []
    choices = []
    for x, y in zip(a, b):
        sx, sy = x[2],y[2]
        if len(sy) != len(sx): return []
        choices.append(list(zip(sx, sy)))
    total = 1
    for c in choices: total *= len(c)
    if total > 64: return []
    result = []
    for combination in itertools.product(*choices):
        values = []
        for text, matches, side in ((source,a,0),(target,b,1)):
            value, start = b'', 0
            for match, option in zip(matches, combination):
                value += text[start:match[0]] + option[side]; start = match[1]
            values.append(value + text[start:])
        result.append(tuple(values))
    return result


def unescape_bytes(text):
    return re.sub(rb'\\(x[0-9a-fA-F]{2}|[\\nrt])',
                  lambda m: bytes([int(m[1][1:],16)]) if m[1].startswith(b'x') else
                  {b'\\':b'\\',b'n':b'\n',b'r':b'\r',b't':b'\t'}[m[1]],text.encode('utf-8'))


def escape_bytes(raw):
    # Keep opaque command operands as bytes; Latin-1->UTF-8 conversion corrupts
    # C3/BA/C6 and NUL operands in random-branch-derived runtime templates.
    result=[];start=pos=0
    while pos<len(raw):
        if raw[pos]==1:
            if pos+2>=len(raw):raise ValueError('Truncated Opal command')
            result.append(escape(raw[start:pos].decode('utf-8')))
            result.append(''.join('\\x%02X'%b for b in raw[pos:pos+3]))
            pos+=3;start=pos
        else:pos+=1
    result.append(escape(raw[start:].decode('utf-8')))
    return ''.join(result)


def escape(text):
    text = text.replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t')
    return re.sub(r'[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]', lambda m: '\\x%02X' % ord(m[0]), text)


def load_supplement(supplement):
    supplement=Path(supplement).resolve()
    report_path=supplement/'TranslateFiles/reports/final_validation.json'
    extra=json.loads(report_path.read_text(encoding='utf-8'))
    extra_target=Path(extra['path']).resolve()
    if extra_target.parent!=supplement or not extra_target.name.endswith('_translated.csv'):
        raise ValueError('Supplement export outside its project')
    extra_source=extra_target.with_name(extra_target.name.replace('_translated.csv','.csv'))
    if (extra['status']!='passed' or extra['source_sha256']!=digest(extra_source)
            or extra['translation_sha256']!=digest(extra_target)):
        raise ValueError('Supplemental formal export hashes do not match')
    a,b=read_csv(extra_source),read_csv(extra_target)
    if len(a)!=len(b) or len(a)!=extra['records']:raise ValueError('Supplement alignment mismatch')
    mapping_path=extra_source.with_suffix('.mapping.json')
    mapping=json.loads(mapping_path.read_text(encoding='utf-8'))
    occurrence_mode='source_csv_sha256' not in mapping
    if mapping.get('source_csv_sha256',mapping.get('source_sha256'))!=digest(extra_source) or len(mapping['records'])!=len(a):
        raise ValueError('Supplement resource mapping mismatch')
    binding_report=None
    if occurrence_mode:
        binding_report=json.loads((supplement/'resource_bindings.json').read_text(encoding='utf-8'))
        if len(binding_report['records'])!=len(a):raise ValueError('Missing occurrence bindings')
    resource_cache={}
    for ordinal,(source_row,record) in enumerate(zip(a,mapping['records']),1):
        if record['ordinal']!=ordinal or record['source_sha256']!=hashlib.sha256(source_row.encode()).hexdigest():
            raise ValueError('Supplement source identity mismatch')
        if occurrence_mode:
            bound=binding_report['records'][ordinal-1]
            expected=f"{binding_report['resource']}#scene={bound['scene']};offset={bound['offset']:X};bytes={bound['bytes']}"
            if record['resource']!=expected or bound['source']!=source_row:
                raise ValueError('Occurrence locator/source mismatch')
            bindings=[dict(bound,resource=binding_report['resource'])]
            expected_resource_hash=binding_report['resource_sha256']
        else:
            bindings=record['bindings'];expected_resource_hash=mapping['resource_sha256']
        for binding in bindings:
            relative=Path(binding['resource'])
            if relative.is_absolute() or '..' in relative.parts:raise ValueError('Resource outside game')
            if relative not in resource_cache:
                raw=(ROOT/'Game'/relative).read_bytes();resource_cache[relative]=(raw,hashlib.sha256(raw).hexdigest())
            raw,rawhash=resource_cache[relative];offset,length=binding['offset'],binding['bytes']
            if (rawhash!=expected_resource_hash or offset<0 or length<1 or offset+2+length>len(raw)
                or int.from_bytes(raw[offset:offset+2],'little')!=length
                or hashlib.sha256(raw[offset+2:offset+2+length]).hexdigest()!=binding['source_bytes_sha256']):
                raise ValueError('Supplement source resource changed')
            if occurrence_mode and unescape_bytes(source_row)!=raw[offset+2:offset+2+length]:
                raise ValueError('Occurrence does not round-trip to original bytes')
    if any('\t' in s or '\n' in s or '\r' in s for s in a+b):raise ValueError('Unescaped supplemental TSV delimiter')
    return list(zip(a,b)),dict(project=str(supplement),records=len(a),validation_sha256=digest(report_path),mapping_sha256=digest(mapping_path))


def build(destination, supplement=None):
    source = ROOT / 'king-of-dragon-pass_text.csv'
    translated = ROOT / 'king-of-dragon-pass_text_translated.csv'
    validation = json.loads((ROOT / 'TranslateFiles/reports/final_validation.json').read_text(encoding='utf-8'))
    if (validation['status'] != 'passed' or validation['source_sha256'] != digest(source)
            or validation['translation_sha256'] != digest(translated)):
        raise ValueError('Approved export hashes do not match')
    originals, translations = read_csv(source), read_csv(translated)
    if len(originals) != len(translations) or len(originals) != validation['records']:
        raise ValueError('CSV alignment mismatch')
    if any('\t' in s or '\n' in s or '\r' in s for s in originals + translations):
        raise ValueError('Unescaped TSV delimiter in corpus')
    by_file = {}
    with (ROOT / 'king-of-dragon-pass_text_catalog.tsv').open(encoding='utf-8-sig', newline='') as f:
        for r in csv.DictReader(f, delimiter='\t'):
            if r['kind'] == 'HTML text':
                relative = Path(r['source'])
                if relative.is_absolute() or '..' in relative.parts or relative.parts[:2] not in (('KoDp', 'Text'), ('KoDp', 'Manual')):
                    raise ValueError('HTML outside whitelist: ' + str(relative))
                by_file.setdefault(relative, {})[r['locator']] = int(r['corpus_index'])
    pages, replaced = [], 0
    for relative, mapping in by_file.items():
        candidates = [ROOT / 'release/KoDPCNHook/backup/html_original' / relative,
                      ROOT / 'Game' / relative]
        for path in candidates:
            if not path.is_file():
                continue
            text = path.read_text(encoding='utf-8-sig')
            seen = set()
            def replace(m):
                locator = f'{m[2].lower()}={replace.ordinal}'
                replace.ordinal += 1
                if locator not in mapping:
                    return m[0]
                index = mapping[locator]
                if normalize(m[3]) != unescape(originals[index]):
                    raise ValueError(f'Original HTML mismatch: {relative}:{locator}')
                seen.add(locator)
                return m[1] + html.escape(unescape(translations[index]), quote=True) + m[4]
            replace.ordinal = 1
            try:
                rebuilt = TAG.sub(replace, text)
                if seen != set(mapping):
                    raise ValueError('HTML catalog locators missing')
            except ValueError:
                continue
            pages.append((relative, rebuilt, len(seen), str(path), digest(path)))
            replaced += len(seen)
            break
        else:
            raise ValueError('No verified original HTML: ' + str(relative))
    pairs = list(zip(originals, translations))
    supplemental_report = []
    supplements = supplement if isinstance(supplement,list) else ([supplement] if supplement else [])
    for extra_project in supplements:
        extra_pairs,extra_report=load_supplement(extra_project)
        pairs.extend(extra_pairs);supplemental_report.append(extra_report)
    # The display hook has no scene/field identity. Never silently choose one
    # translation when resource occurrences of a source disagree. Keep every
    # occurrence in the report, and omit that key from the global dictionary.
    groups={}
    for index,(s,t) in enumerate(pairs):
        groups.setdefault(unescape_bytes(s),[]).append((index,s,t))
    conflicts=[];safe=[];excluded_raw=set()
    for raw,occurrences in groups.items():
        targets={unescape_bytes(t) for _,_,t in occurrences}
        if len(targets)>1:
            excluded_raw.add(raw)
            conflicts.append(dict(source=occurrences[0][1],occurrences=[dict(input_index=i,translation=t) for i,_,t in occurrences],policy='excluded: display hook has no resource identity'))
        else:safe.append((occurrences[0][1],occurrences[0][2]))
    pairs=safe
    approved_raw={unescape_bytes(s):unescape_bytes(t) for s,t in pairs}
    derived = {}
    ambiguous = set()
    for s, t in pairs:
        for es, et in expanded_pairs(unescape_bytes(s), unescape_bytes(t)):
            if es in excluded_raw:continue
            if es in approved_raw:
                if approved_raw[es]!=et:ambiguous.add(escape_bytes(es))
                continue
            key, value = escape_bytes(es), escape_bytes(et)
            if key in ambiguous: continue
            if key in derived and derived[key] != value:
                derived.pop(key); ambiguous.add(key); continue
            derived[key] = value
    # The displayed clan-history field omits its leading selection marker.
    for s, t in pairs:
        if s.startswith('?To prepare for the marriage of Ernalda and Orlanth,') and t.startswith('?'):
            derived[s[1:]] = t[1:]
    destination.mkdir(parents=True, exist_ok=False)
    (destination/'DICTIONARY_CONFLICTS.json').write_text(json.dumps(conflicts,ensure_ascii=False,indent=2),encoding='utf-8')
    dictionary = destination / 'translations.tsv'
    dictionary.write_text(''.join(s + '\t' + t + '\n' for s, t in pairs + list(derived.items())), encoding='utf-8', newline='')
    files = [{'path': dictionary.name, 'sha256': digest(dictionary), 'role': 'runtime_dictionary'}]
    for relative, text, count, origin, origin_hash in pages:
        output = destination / 'overlay' / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding='utf-8', newline='')
        files.append({'path': output.relative_to(destination).as_posix(), 'sha256': digest(output),
                      'role': 'html', 'records': count, 'verified_original': origin, 'original_sha256': origin_hash})
    report = {'status': 'built_pending_runtime', 'translation_sha256': digest(translated),
              'source_sha256': digest(source), 'dictionary_records': len(pairs),
              'conflicting_source_keys_excluded': len(conflicts),
              'derived_runtime_records': len(derived), 'ambiguous_variants_excluded': len(ambiguous),
              'supplement': supplemental_report,
              'html_files': len(pages), 'html_records': replaced, 'files': files,
              'game_files_modified': False, 'image_adapter': 'pending'}
    (destination / 'BUILD_MANIFEST.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'files'}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--supplement', type=Path, action='append')
    args = parser.parse_args()
    build(args.output.resolve(), args.supplement)
