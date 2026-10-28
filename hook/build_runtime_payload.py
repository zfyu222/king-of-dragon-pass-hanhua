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


def build(destination):
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
    destination.mkdir(parents=True, exist_ok=False)
    dictionary = destination / 'translations.tsv'
    dictionary.write_text(''.join(s + '\t' + t + '\n' for s, t in zip(originals, translations)), encoding='utf-8', newline='')
    files = [{'path': dictionary.name, 'sha256': digest(dictionary), 'role': 'runtime_dictionary'}]
    for relative, text, count, origin, origin_hash in pages:
        output = destination / 'overlay' / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding='utf-8', newline='')
        files.append({'path': output.relative_to(destination).as_posix(), 'sha256': digest(output),
                      'role': 'html', 'records': count, 'verified_original': origin, 'original_sha256': origin_hash})
    report = {'status': 'built_pending_runtime', 'translation_sha256': digest(translated),
              'source_sha256': digest(source), 'dictionary_records': len(originals),
              'html_files': len(pages), 'html_records': replaced, 'files': files,
              'game_files_modified': False, 'image_adapter': 'pending'}
    (destination / 'BUILD_MANIFEST.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'files'}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    build(parser.parse_args().output.resolve())
