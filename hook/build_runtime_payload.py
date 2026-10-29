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
    pattern = re.compile('\x02([\x01-\x1f])([^\x02]*)\x02')
    a, b = list(pattern.finditer(source)), list(pattern.finditer(target))
    if not a or len(a) != len(b): return []
    choices = []
    for x, y in zip(a, b):
        sx, sy = x[2].split('/'), y[2].split('/')
        if len(sx) != ord(x[1]) or len(sy) != len(sx): return []
        choices.append(list(zip(sx, sy)))
    total = 1
    for c in choices: total *= len(c)
    if total > 64: return []
    result = []
    for combination in itertools.product(*choices):
        values = []
        for text, matches, side in ((source,a,0),(target,b,1)):
            value, start = '', 0
            for match, option in zip(matches, combination):
                value += text[start:match.start()] + option[side]; start = match.end()
            values.append(value + text[start:])
        result.append(tuple(values))
    return result


def escape(text):
    text = text.replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t')
    return re.sub(r'[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]', lambda m: '\\x%02X' % ord(m[0]), text)


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
    supplemental_report = None
    if supplement:
        supplement = Path(supplement).resolve()
        report_path = supplement / 'TranslateFiles/reports/final_validation.json'
        extra = json.loads(report_path.read_text(encoding='utf-8'))
        extra_source = supplement / 'feedback_text.csv'
        extra_target = supplement / 'feedback_text_translated.csv'
        if (extra['status'] != 'passed' or extra['source_sha256'] != digest(extra_source)
                or extra['translation_sha256'] != digest(extra_target)):
            raise ValueError('Supplemental formal export hashes do not match')
        extra_a, extra_b = read_csv(extra_source), read_csv(extra_target)
        if len(extra_a) != len(extra_b) or len(extra_a) != extra['records']:
            raise ValueError('Supplemental alignment mismatch')
        mapping_path = supplement / 'feedback_text.mapping.json'
        mapping = json.loads(mapping_path.read_text(encoding='utf-8'))
        if mapping['source_csv_sha256'] != digest(extra_source) or len(mapping['records']) != len(extra_a):
            raise ValueError('Supplemental resource mapping mismatch')
        for ordinal, (source_row, record) in enumerate(zip(extra_a, mapping['records']), 1):
            if record['ordinal'] != ordinal or record['source_sha256'] != hashlib.sha256(source_row.encode()).hexdigest():
                raise ValueError('Supplemental source identity mismatch')
            for binding in record['bindings']:
                relative = Path(binding['resource'])
                if relative.is_absolute() or '..' in relative.parts:
                    raise ValueError('Resource path outside game')
                resource = ROOT / 'Game' / relative
                raw = resource.read_bytes()
                offset, length = binding['offset'], binding['bytes']
                if (hashlib.sha256(raw).hexdigest() != mapping['resource_sha256'] or offset < 0
                        or length < 1 or offset + 2 + length > len(raw)
                        or int.from_bytes(raw[offset:offset+2], 'little') != length
                        or hashlib.sha256(raw[offset+2:offset+2+length]).hexdigest() != binding['source_bytes_sha256']):
                    raise ValueError('Supplemental source resource changed')
        if any('\t' in s or '\n' in s or '\r' in s for s in extra_a + extra_b):
            raise ValueError('Unescaped supplemental TSV delimiter')
        existing = dict(pairs)
        if any(s in existing and existing[s] != t for s,t in zip(extra_a,extra_b)):
            raise ValueError('Supplement conflicts with approved dictionary')
        pairs.extend(zip(extra_a, extra_b))
        supplemental_report = dict(project=str(supplement), records=len(extra_a),
                                   validation_sha256=digest(report_path), mapping_sha256=digest(mapping_path))
    derived = {}
    ambiguous = set()
    for s, t in pairs:
        for es, et in expanded_pairs(unescape(s), unescape(t)):
            key, value = escape(es), escape(et)
            if key in ambiguous: continue
            if key in derived and derived[key] != value:
                derived.pop(key); ambiguous.add(key); continue
            derived[key] = value
    # The displayed clan-history field omits its leading selection marker.
    for s, t in pairs:
        if s.startswith('?To prepare for the marriage of Ernalda and Orlanth,') and t.startswith('?'):
            derived[s[1:]] = t[1:]
    destination.mkdir(parents=True, exist_ok=False)
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
              'source_sha256': digest(source), 'dictionary_records': len(originals),
              'derived_runtime_records': len(derived), 'ambiguous_variants_excluded': len(ambiguous),
              'supplement': supplemental_report,
              'html_files': len(pages), 'html_records': replaced, 'files': files,
              'game_files_modified': False, 'image_adapter': 'pending'}
    (destination / 'BUILD_MANIFEST.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'files'}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--supplement', type=Path)
    args = parser.parse_args()
    build(args.output.resolve(), args.supplement)
