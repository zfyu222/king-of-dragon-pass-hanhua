"""Bind the approved text export, reviewed images and native archive pixel patch."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(package, image_build):
    package, image_build = package.resolve(), image_build.resolve()
    game = (ROOT / 'Game').resolve()
    if sha(game / 'King of Dragon Pass.s86') != 'bdc90adacde29b6e86ece3886f28ba60c97a7a67f0e3bab7a1d2bb1b641138c5':
        raise ValueError('Only the verified Steam 2.4.0 native layout is supported')
    build_report = json.loads((package / 'BUILD_MANIFEST.json').read_text(encoding='utf-8'))
    images = json.loads((image_build / 'IMAGE_PATCH_MANIFEST.json').read_text(encoding='utf-8'))
    validated = json.loads((ROOT / 'TranslateFiles/image-text/TranslateFiles/reports/final_validation.json').read_text(encoding='utf-8'))
    if images['source_sha256'] != validated['source_sha256'] or images['translation_sha256'] != validated['translation_sha256']:
        raise ValueError('Image translation validation binding failed')
    install = []
    def add(target, payload, **extra):
        install.append(dict(game_path=target, payload=payload.relative_to(package).as_posix(),
            payload_sha256=sha(payload), before_sha256=sha(game / target),
            after_sha256=sha(payload), mode='copy', **extra))
    for f in build_report['files']:
        if f['role'] != 'html':
            continue
        payload = package / f['path']
        relative = Path(f['path']).relative_to('overlay')
        add(relative.as_posix(), payload)
        if relative.parts[1] == 'Text':
            duplicate = Path('KoDp/_Text') / Path(*relative.parts[2:])
            if (game / duplicate).is_file():
                add(duplicate.as_posix(), payload)
    logo_file = None
    for f in images['files']:
        original = image_build / f['path']
        if sha(original) != f['sha256']:
            raise ValueError('Image build changed')
        source = f['sources'][0]['path']
        relative = Path('KoDp/textures') / source
        payload = package / 'image_files' / relative
        payload.parent.mkdir(parents=True, exist_ok=True)
        if relative.suffix.lower() == '.jpg':
            Image.open(original).convert('RGB').save(payload, quality=98, subsampling=0)
        else:
            shutil.copyfile(original, payload)
        if Image.open(payload).size != Image.open(game / relative).size:
            raise ValueError('Image dimensions changed')
        add(relative.as_posix(), payload)
        if source == 'Menu/Logo.png':
            logo_file = payload
    # This is an observed exact byte match, not a guessed texture structure.
    archive = game / 'KoDP480.group.bin'
    old_pixels = Image.open(game / 'KoDp/textures/Menu/Logo.png').convert('RGBA').tobytes()
    before = archive.read_bytes()
    offset = before.find(old_pixels)
    if offset < 0 or before.find(old_pixels, offset + 1) >= 0:
        raise ValueError('Need one unambiguous original RGBA logo allocation')
    new_pixels = Image.open(logo_file).convert('RGBA').tobytes()
    if len(new_pixels) != len(old_pixels):
        raise ValueError('Raw texture allocation changed')
    patch = package / 'image_files/logo480.rgba'
    patch.write_bytes(new_pixels)
    after = before[:offset] + new_pixels + before[offset + len(old_pixels):]
    install.append(dict(game_path=archive.name, payload=patch.relative_to(package).as_posix(),
        payload_sha256=sha(patch), before_sha256=sha(archive),
        after_sha256=hashlib.sha256(after).hexdigest(), mode='rgba_patch',
        offset=offset, length=len(old_pixels), original_pixels_sha256=hashlib.sha256(old_pixels).hexdigest()))
    (package / 'InstallTest.ps1').write_text((ROOT / 'hook/install_test.ps1').read_text(encoding='utf-8'), encoding='utf-8-sig')
    exe = game / 'King of Dragon Pass.exe'
    (package / 'KoDPCNHook.ini').write_text('[Launcher]\nGameExe=' + str(exe) + '\n', encoding='utf-8')
    launch = '@echo off\r\nchcp 65001 >nul\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0InstallTest.ps1"\r\nif errorlevel 1 (pause & exit /b 1)\r\nstart "" "%~dp0KoDPCNLauncher.exe"\r\n'
    restore = '@echo off\r\nchcp 65001 >nul\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0InstallTest.ps1" -Restore\r\npause\r\n'
    (package / '启动测试.cmd').write_bytes(launch.encode('utf-8'))
    (package / '恢复测试前状态.cmd').write_bytes(restore.encode('utf-8'))
    report = dict(status='ready_for_user_test', game_version='Steam 2.4.0 Windows',
        default_game_root=str(game), game_s86_sha256=sha(game / 'King of Dragon Pass.s86'),
        translation_sha256=build_report['translation_sha256'], image_build=str(image_build),
        support_files=[dict(path=name, sha256=sha(package / name)) for name in ('KoDPCNHook.dll', 'KoDPCNLauncher.exe', 'translations.tsv', 'InstallTest.ps1')],
        image_files=len(images['files']), html_targets=sum(f['game_path'].endswith('.html') for f in install),
        install_files=install, runtime_validation='pending_user_test',
        image_adapter='loose resource paths plus exact in-place KoDP480 RGBA logo patch')
    (package / 'TEST_MANIFEST.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    build_report.update(status='ready_for_user_test', image_adapter=report['image_adapter'], image_build=str(image_build))
    (package / 'BUILD_MANIFEST.json').write_text(json.dumps(build_report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'install_files'}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--package', type=Path, required=True)
    p.add_argument('--images', type=Path, required=True)
    a = p.parse_args()
    build(a.package, a.images)
