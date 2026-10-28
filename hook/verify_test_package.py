"""Exercise player PowerShell install/idempotence/restore on an isolated fixture."""
import argparse
import hashlib
import json
import shutil
import subprocess
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def verify(package):
    package = package.resolve()
    m = json.loads((package / 'TEST_MANIFEST.json').read_text(encoding='utf-8'))
    for e in m['install_files']:
        if sha(ROOT / 'Game' / e['game_path']) != e['before_sha256']:
            raise AssertionError('Source game changed before test: ' + e['game_path'])
    fixture = package.parent / ('verification_' + uuid.uuid4().hex[:8])
    clone, game = fixture / 'package', fixture / 'game'
    clone.mkdir(parents=True); game.mkdir()
    paths = {'TEST_MANIFEST.json', 'InstallTest.ps1'} | {e['payload'] for e in m['install_files']} | {e['path'] for e in m['support_files']}
    for rel in paths:
        out = clone / rel; out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(package / rel, out)
    shutil.copyfile(ROOT / 'Game/King of Dragon Pass.s86', game / 'King of Dragon Pass.s86')
    for e in m['install_files']:
        out = game / e['game_path']; out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'Game' / e['game_path'], out)
    results = []
    def run(name, restore=False, success=True):
        command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(clone / 'InstallTest.ps1'), '-GameRoot', str(game)]
        if restore: command.append('-Restore')
        r = subprocess.run(command, capture_output=True)
        (fixture / (name + '.log')).write_bytes(r.stdout + b'\n' + r.stderr)
        if (r.returncode == 0) != success:
            raise RuntimeError(f'{name} unexpected exit {r.returncode}; see {fixture}')
        results.append(dict(check=name, exit_code=r.returncode, expected_success=success))
    def check(field):
        for e in m['install_files']:
            if sha(game / e['game_path']) != e[field]: raise AssertionError(e['game_path'])
    run('install'); check('after_sha256')
    run('idempotent_install'); check('after_sha256')
    run('restore', restore=True); check('before_sha256')
    # Corruption must fail preflight without altering any game target.
    broken = clone / m['install_files'][0]['payload']
    broken.write_bytes(broken.read_bytes() + b'tampered')
    run('reject_tampered_payload', success=False); check('before_sha256')
    for e in m['install_files']:
        if sha(ROOT / 'Game' / e['game_path']) != e['before_sha256']:
            raise AssertionError('Source game changed during test: ' + e['game_path'])
    report = dict(status='passed', installed_and_restored=len(m['install_files']),
        source_game_untouched=True, fixture=str(fixture), checks=results,
        manifest_sha256=sha(package / 'TEST_MANIFEST.json'))
    out = ROOT / 'Analysis/repack_test_verification_20261029.json'
    out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--package', type=Path, required=True)
    verify(p.parse_args().package)
