"""Reuse verified image/HTML payloads and recovery backups for a new hook build.

The old package and live game remain unchanged. Build the dictionary and DLL
in a fresh output directory before invoking this adapter.
"""
import argparse
import hashlib
import json
import shutil
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def finish(base, output):
    base, output = base.resolve(), output.resolve()
    if base == output or base in output.parents or output in base.parents:
        raise ValueError('Need separate base and output packages')
    manifest = json.loads((base / 'TEST_MANIFEST.json').read_text(encoding='utf-8'))
    paths = {'InstallTest.ps1', 'KoDPCNHook.ini', '启动测试.cmd', '恢复测试前状态.cmd'}
    for entry in manifest['install_files']:
        payload = base / entry['payload']
        if sha(payload) != entry['payload_sha256']:
            raise ValueError('Base payload changed: ' + entry['payload'])
        paths.add(entry['payload'])
        backup = Path('backup/before_test') / entry['game_path']
        if (base / backup).exists():
            if sha(base / backup) != entry['before_sha256']:
                raise ValueError('Base backup changed: ' + str(backup))
            paths.add(backup.as_posix())
    if (base / 'backup/test_state.json').is_file():
        paths.add('backup/test_state.json')
    for rel in paths:
        target = output / rel
        if target.exists() and sha(target) != sha(base / rel):
            raise ValueError('Output already differs: ' + rel)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(base / rel, target)
    for entry in manifest['support_files']:
        entry['sha256'] = sha(output / entry['path'])
    manifest['runtime_validation'] = 'offline_screenshot_regressions_passed_pending_user_test'
    manifest['base_manifest_sha256'] = sha(base / 'TEST_MANIFEST.json')
    manifest['feedback_version'] = '20261029.1'
    (output / 'TEST_MANIFEST.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    build = json.loads((output / 'BUILD_MANIFEST.json').read_text(encoding='utf-8'))
    build.update(status='ready_for_user_test', image_build=manifest['image_build'],
                 image_adapter=manifest['image_adapter'])
    (output / 'BUILD_MANIFEST.json').write_text(json.dumps(build, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(dict(status='ready_for_user_test', output=str(output), targets=len(manifest['install_files']))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--base', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    finish(args.base, args.output)
