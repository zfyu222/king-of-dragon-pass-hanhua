"""Read-only takeover audit; never changes workflow state or translations."""
import csv
import json
import sqlite3
import sys
from collections import Counter
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root.parents[1] / 'Tools/localization'))
import ainiee_bridge as bridge

db = sqlite3.connect((root / 'TranslateFiles/state/workflow.sqlite3').as_uri() + '?mode=ro', uri=True)
db.row_factory = sqlite3.Row
config = json.loads((root / 'TranslateFiles/config/workflow.json').read_text(encoding='utf-8'))
rules = bridge.patterns(False, config['exclusions'])
rows = list(db.execute('select * from records order by ordinal'))
print('records:', dict(Counter(r['stage'] for r in rows)))
for table in ('tasks', 'review_candidates', 'api_calls'):
    print(table, [tuple(r) for r in db.execute(f'select status,count(*) from {table} group by status')])
print('integrity:', db.execute('pragma quick_check').fetchone()[0])
issues = []
for r in rows:
    found = bridge.record_structural_issues(dict(r), r['translation'], rules, config)
    if found:
        issues.append((r['id'], found))
print('full live structural failures:', len(issues), issues[:10])
for suffix, field in (('_text.csv', 'source'), ('_text_translated.csv', 'translation')):
    with (root / ('king-of-dragon-pass' + suffix)).open(encoding='utf-8-sig', newline='') as f:
        csvrows = list(csv.reader(f))
    print(suffix, 'records:', len(csvrows), 'non-single-column:', sum(len(r) != 1 for r in csvrows))
    print('differences from DB:', sum(a != [b[field]] for a, b in zip(csvrows, rows)), 'length delta:', len(csvrows)-len(rows))
ids = json.loads((root / 'TranslateFiles/reports/review_round_status.json').read_text(encoding='utf-8'))['needs_attention_ids']
for r in rows:
    if r['id'] in ids:
        print('\nSTOPPED', r['id'], '\nEN', r['source'], '\nLIVE', r['translation'])
        item = db.execute("select item from review_candidates where record_id=? and stage='review'", (r['id'],)).fetchone()
        if item:
            print('CANDIDATE', item[0])
print('\nDETERMINISTIC SAMPLE (10, ordinal spaced):')
for r in rows[::3442][:10]:
    print(r['id'], '\nEN', r['source'], '\nZH', r['translation'])
