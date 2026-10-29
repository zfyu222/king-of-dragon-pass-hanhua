"""King of Dragon Pass native resource patch release adapter.

Only patch pixels are distributed for the group archive, never the archive.
"""
import hashlib
import importlib.util
import json
import shutil
import sys
import zipfile
from pathlib import Path, PurePosixPath
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
sys.path.insert(0, str(WORKSPACE / 'Tools/localization'))
from translation_workflow import Workflow


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def write(p, x):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(x, ensure_ascii=False, indent=2), encoding='utf-8')


def build():
    from PIL import Image
    cfg = json.loads((ROOT / 'ReleaseConfig/release.json').read_text(encoding='utf-8'))
    old = ROOT / 'Repack/Feedback_20261029/KoDPCNHook'
    native = ROOT / 'Repack/Release_1.1/KoDPCNHook'
    base = json.loads((old / 'TEST_MANIFEST.json').read_text(encoding='utf-8'))
    titles = ROOT / 'ImagePatch/title-final-20261029'
    title_manifest = json.loads((titles / 'IMAGE_PATCH_MANIFEST.json').read_text(encoding='utf-8'))
    previous = json.loads((ROOT / 'ImagePatch/build-v15/IMAGE_PATCH_MANIFEST.json').read_text(encoding='utf-8'))
    title_sources = {f['image_id']:f['sources'][0]['path'] for f in previous['files']}
    replacements = {title_sources[f['image_id']]: titles / f['path'] for f in title_manifest['files']}
    out = ROOT / 'Dispatch/Files'
    if out.exists():
        raise ValueError('Use a fresh Dispatch/Files staging directory')
    out.mkdir(parents=True)
    entries = []

    def add(source, destination, role, **extra):
        target = out / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        entry = dict(path=extra.pop('game_path', destination), archive_path=destination,
                     role=role, sha256=sha(target), size=target.stat().st_size, **extra)
        entries.append(entry)
        return entry

    for name in ('KoDPCNHook.dll','KoDPCNLauncher.exe','translations.tsv'):
        add((old if name == 'translations.tsv' else native) / name, '汉化文件/KoDPCNHook/'+name, 'runtime')
    legacy_manifest = json.loads((ROOT / 'Dispatch/PATCH_MANIFEST.json').read_text(encoding='utf-8'))
    legacy_html = {e['path'].replace('KoDPCNHook/overlay/',''):e['sha256'] for e in legacy_manifest['files'] if '/overlay/' in e['path']}
    for e in base['install_files']:
        source = old / e['payload']
        if sha(source) != e['payload_sha256']:
            raise ValueError('Test payload changed')
        original_hash = e['before_sha256']
        if e['game_path'].endswith('.html'):
            original = ROOT / 'release/KoDPCNHook/backup/html_original' / e['game_path']
            original_hash = sha(original)
        texture = e['game_path'].removeprefix('KoDp/textures/')
        if texture in replacements:
            source = replacements[texture]
        installed_hash = sha(source)
        extra = dict(mode=e['mode'],original_sha256=original_hash,
                     legacy_sha256=list({e['before_sha256'], e['after_sha256']}),game_path=e['game_path'])
        if e['game_path'] in legacy_html:
            extra['legacy_sha256'].append(legacy_html[e['game_path']])
        if e['mode'] == 'rgba_patch':
            source = native / 'logo480.rgba'
            source.write_bytes(Image.open(replacements['Menu/Logo.png']).convert('RGBA').tobytes())
            original = old / 'backup/before_test' / e['game_path']
            if sha(original) != original_hash:
                raise ValueError('Original archive binding changed')
            raw = bytearray(original.read_bytes())
            pixels = source.read_bytes()
            if len(pixels) != e['length']:
                raise ValueError('Logo allocation changed')
            raw[e['offset']:e['offset']+e['length']] = pixels
            installed_hash = hashlib.sha256(raw).hexdigest()
            extra.update(offset=e['offset'],length=e['length'])
        extra['installed_sha256'] = installed_hash
        add(source, '汉化文件/资源/'+('logo480.rgba' if e['mode']=='rgba_patch' else e['game_path']), 'resource', **extra)
    auxiliary=[]
    for action, name in [('install','安装汉化'),('launch','启动汉化'),('uninstall','卸载汉化')]:
        target = out / (name+'.cmd')
        target.write_bytes(('@echo off\r\nchcp 65001 >nul\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0汉化工具\\install.ps1" -Action '+action+'\r\nif errorlevel 1 (pause & exit /b 1)\r\n'+('' if action=='launch' else 'pause\r\n')).encode('utf-8'))
        auxiliary.append(dict(path=target.name,archive_path=target.name,sha256=sha(target),size=target.stat().st_size))
    script=out/'汉化工具/install.ps1';script.parent.mkdir(parents=True,exist_ok=True)
    script.write_text((ROOT/'ReleaseConfig/install.ps1').read_text(encoding='utf-8'),encoding='utf-8-sig')
    auxiliary.append(dict(path='汉化工具/install.ps1',archive_path='汉化工具/install.ps1',sha256=sha(script),size=script.stat().st_size))
    manifest=dict(archive=f"{cfg['game_name']}_子非鱼汉化{cfg['version']}.zip",created='2026-10-30',
        install_mode='script',engine_adapter='Marmalade native hook and bounded RGBA pixel patch',
        game_s86_sha256=base['game_s86_sha256'],files=entries,auxiliary=auxiliary)
    write(out/'PATCH_MANIFEST.json',manifest)
    finalize(out, manifest, entries, auxiliary, cfg)


def finalize(out, manifest, entries, auxiliary, cfg):
    for e in entries+auxiliary:
        if sha(out/e['archive_path']) != e['sha256']:
            raise ValueError('Staged file changed: '+e['archive_path'])
    evidence=ROOT/'ReleaseConfig/USER_RELEASE_AUTHORIZATION_20261030.md'
    evidence.write_text('用户原话：发布吧，另外中文名字改为"龙城之王"\n\n此前已告知本版实机复测待完成和6692处待核实候选；本次按明确发布指令记录独立发布授权，保持实机验证pending。\n',encoding='utf-8')
    payload=[dict(path=(out/e['archive_path']).relative_to(ROOT).as_posix(),sha256=e['sha256'],game_path=e['path']) for e in entries+auxiliary]
    report=dict(authorization='explicit_user_release',accept_pending_runtime=True,in_game_validation='pending',
        user_statement='发布吧，另外中文名字改为"龙城之王"',game_version='Steam v2.4.0',platform='Steam（Windows）',
        payload_files=payload,evidence_files=[dict(path=evidence.relative_to(ROOT).as_posix(),sha256=sha(evidence))])
    for project in (ROOT, ROOT/'TranslateFiles/feedback-20261029'):
        flow=Workflow(project)
        export=json.loads((project/'TranslateFiles/reports/final_validation.json').read_text(encoding='utf-8'))
        bound=dict(report,translation_sha256=export['translation_sha256'])
        if project != ROOT:
            snapshot=project/'ReleasePayload'
            if not snapshot.exists():shutil.copytree(out,snapshot)
            shutil.copyfile(evidence,project/'USER_RELEASE_AUTHORIZATION.md')
            bound['payload_files']=[dict(path='ReleasePayload/'+e['archive_path'],sha256=e['sha256'],game_path=e['path']) for e in entries+auxiliary]
            bound['evidence_files']=[dict(path='USER_RELEASE_AUTHORIZATION.md',sha256=sha(evidence))]
        flow.record_release_acceptance(bound)
        print(flow.release_check()['authorization'])
    write(ROOT/'ReleaseConfig/payload_bindings.json',report)
    spec=importlib.util.spec_from_file_location('release_common',WORKSPACE/'.agents/skills/game-release-patch/scripts/create_release_patch.py')
    common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)
    common.check_custom_quality(ROOT,[(out/e['archive_path'],PurePosixPath(e['path'])) for e in entries+auxiliary],report['game_version'],report['platform'])
    args=SimpleNamespace(title='《龙城之王》子非鱼AI汉化补丁 v1.1',
        install='解压到 King of Dragon Pass.exe 所在文件夹，双击“启动汉化.cmd”。',
        upgrade='覆盖解压新版补丁后，运行“安装汉化.cmd”。',uninstall='运行“卸载汉化.cmd”。',
        introduction='为《King of Dragon Pass（龙城之王）》制作的简体中文汉化补丁。',
        game_version=report['game_version'],platform=report['platform'],
        coverage='界面、氏族管理、顾问提示、剧情、地图文字、神话插图页和内置手册。',
        download_url=cfg['download_url'],mirror_url=cfg['mirror_url'],share_name=cfg['share_name'],
        feedback_url=cfg['feedback_url'],feedback_name=cfg['feedback_name'],game_name=cfg['game_name'],
        release_date='2026-10-30',changes='更新重译词库；修复人物提示、史书、氏族历史、劫牛及战报漏翻；更新图片文字与中文标题；增加安装、启动和卸载入口。')
    guide=common.make_guide(args,'1.1','- v1.0：首次发布\n')
    (out/'补丁说明.txt').write_text(guide,encoding='utf-8-sig')
    (out/'SHA256SUMS.txt').write_text(''.join(e['sha256']+'  '+e['archive_path']+'\n' for e in entries+auxiliary),encoding='utf-8')
    (ROOT/'ReleaseConfig/release_body.md').write_text(guide,encoding='utf-8')
    print('Staged',len(entries),'payloads and',len(auxiliary),'helpers; ZIP waits for installer validation')


if __name__=='__main__':
    if '--finalize' in sys.argv:
        out=ROOT/'Dispatch/Files'
        manifest=json.loads((out/'PATCH_MANIFEST.json').read_text(encoding='utf-8'))
        finalize(out,manifest,manifest['files'],manifest['auxiliary'],json.loads((ROOT/'ReleaseConfig/release.json').read_text(encoding='utf-8')))
    else:
        build()
