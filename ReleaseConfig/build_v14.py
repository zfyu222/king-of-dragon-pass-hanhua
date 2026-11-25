"""Stage the polished v1.4 text on top of the verified v1.3 installer payload."""
import importlib.util
import json
import shutil
import sys
from pathlib import PurePosixPath
from types import SimpleNamespace

from build_release import ROOT, WORKSPACE, sha, write

VERSION = '1.4'
DATE = '2026-11-26'
GAME_VERSION = 'Steam v2.4.0'
PLATFORM = 'Steam（Windows）'
ARCHIVE = f'龙城之王_子非鱼汉化{VERSION}.zip'
CONFIG = ROOT / 'ReleaseConfig'
DISPATCH = ROOT / 'Dispatch'
FILES = DISPATCH / 'Files'
RETAINED = DISPATCH / 'Files_v1.3'
RUNTIME = ROOT / 'Repack/PolishRuntime_v1.4'


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def common_module():
    spec = importlib.util.spec_from_file_location(
        'release_common', WORKSPACE / '.agents/skills/game-release-patch/scripts/create_release_patch.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def dictionary_keys(path):
    return {line.split(b'\t', 1)[0] for line in path.read_bytes().split(b'\n') if line}


def stage():
    common = common_module()
    cfg = read(CONFIG / 'release.json')
    assert cfg['version'] in ('1.3', VERSION) and common.next_version(DISPATCH, cfg['game_name']) == VERSION
    manifest = read(FILES / 'PATCH_MANIFEST.json')
    assert manifest['archive'] == '龙城之王_子非鱼汉化1.3.zip'
    build = read(RUNTIME / 'BUILD_MANIFEST.json')
    formal = read(ROOT / 'TranslateFiles/reports/final_validation.json')
    assert build['translation_sha256'] == formal['translation_sha256']
    rebuilt = {item['path']: item for item in build['files']}
    originals = ROOT / 'release/KoDPCNHook/backup/html_original'
    payload = manifest['files'] + manifest['auxiliary']
    by_path = {item['path']: dict(item) for item in manifest['files']}
    assert len({item['archive_path'] for item in payload}) == len(payload)
    sources = {}
    changed_html = 0
    for item in payload:
        path = item['path']
        archive_path = item['archive_path']
        previous = FILES / archive_path
        assert sha(previous) == item['sha256'], path
        source = previous
        if item.get('role') == 'runtime' and path.endswith('/translations.tsv'):
            source = RUNTIME / 'translations.tsv'
            assert sha(source) == rebuilt['translations.tsv']['sha256']
            assert dictionary_keys(previous) <= dictionary_keys(source), 'An existing runtime key was lost'
        elif item.get('role') == 'resource' and path.endswith('.html'):
            canonical = path.replace('KoDp/_Text/', 'KoDp/Text/', 1)
            assert sha(originals / path) == item['original_sha256'], path
            # Text and _Text have separate original hashes and deliberately shared translated pages.
            assert item['sha256'] == by_path[canonical]['sha256'], path
            source = RUNTIME / 'overlay' / canonical
            assert sha(source) == rebuilt[f'overlay/{canonical}']['sha256'], path
            assert by_path[canonical]['original_sha256'] == rebuilt[f'overlay/{canonical}']['original_sha256']
            installed = sha(source)
            if installed != item['installed_sha256']:
                item['legacy_sha256'] = sorted(set(item['legacy_sha256']) | {item['installed_sha256']})
                item['installed_sha256'] = installed
                changed_html += 1
        sources[archive_path] = source
        item.update(sha256=sha(source), size=source.stat().st_size)

    candidate = DISPATCH / 'Files_v1.4_stage'
    for path in (candidate, FILES, RETAINED):
        assert path.resolve().parent == DISPATCH.resolve()
    assert not candidate.exists() and not RETAINED.exists()
    candidate.mkdir()
    for relative, source in sources.items():
        target = candidate / relative
        assert target.resolve().is_relative_to(candidate.resolve())
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    manifest.update(archive=ARCHIVE, created=DATE)
    write(candidate / 'PATCH_MANIFEST.json', manifest)
    cfg['version'] = VERSION
    cfg['steam_url'] = 'https://store.steampowered.com/app/352220/'
    args = SimpleNamespace(
        **cfg, title='[子非鱼Ai汉化]《龙城之王》/《King of Dragon Pass》', release_date=DATE,
        game_version=GAME_VERSION, platform=PLATFORM,
        introduction='这是一款以格洛兰萨为背景的氏族经营与互动叙事游戏。你将带领氏族分配资源、听取顾问意见，在外交、战争和神话事件中做出抉择，逐步扩大氏族的影响力。',
        install='解压到 King of Dragon Pass.exe 所在目录，双击“龙城之王_中文版.exe”，首次运行自动安装并启动，以后直接启动。',
        upgrade='覆盖解压新版补丁后，运行同一个“龙城之王_中文版.exe”。',
        uninstall='运行“卸载汉化.cmd”。',
        coverage='界面、氏族管理、顾问提示、剧情、地图文字、神话插图页和内置手册。',
        changes='润色主译文并复审全部改动，更新剧情、界面和手册等文字。')
    guide = common.make_guide(args, VERSION, common.previous_changelog(DISPATCH, cfg['game_name'], VERSION))
    launch = '请运行“龙城之王_中文版.exe”；直接运行原游戏EXE不能启用本汉化。'
    notice = f'【重要：启动中文版】\n{launch}\n\n'
    guide = guide.replace('\n游戏介绍\n', '\n' + notice + '游戏介绍\n', 1)
    (candidate / '补丁说明.txt').write_text(guide, encoding='utf-8-sig')
    cover = f'https://github.com/{cfg["repository"]}/releases/download/v{VERSION}/title.jpg'
    body = guide.replace(notice, f'**【重要：启动中文版】{launch}**\n\n', 1)
    (CONFIG / 'release_body.md').write_text(f'![龙城之王 子非鱼汉化]({cover})\n\n{body}', encoding='utf-8')
    sums = ''.join(f'{sha(path)}  {path.relative_to(candidate).as_posix()}\n'
                   for path in sorted(candidate.rglob('*')) if path.is_file())
    (candidate / 'SHA256SUMS.txt').write_text(sums, encoding='utf-8')
    FILES.rename(RETAINED)
    candidate.rename(FILES)
    write(CONFIG / 'release.json', cfg)
    print(json.dumps({'version': VERSION, 'payload_files': len(payload), 'changed_html': changed_html}, ensure_ascii=False))


def authorize():
    sys.path.insert(0, str(WORKSPACE / 'Tools/localization'))
    from translation_workflow import Workflow

    manifest = read(FILES / 'PATCH_MANIFEST.json')
    assert manifest['archive'] == ARCHIVE
    payload = manifest['files'] + manifest['auxiliary']
    verification = read(CONFIG / 'installation_validation.json')
    assert verification['status'] == 'passed' and verification['manifest_sha256'] == sha(FILES / 'PATCH_MANIFEST.json')
    evidence = CONFIG / f'USER_RELEASE_AUTHORIZATION_v{VERSION}.md'
    evidence.write_text('2026-11-26 用户在获知最新译文尚待回填验证后明确回复“发布吧”。\n', encoding='utf-8')
    checks = []
    for project in (ROOT, *(ROOT / 'TranslateFiles' / name for name in (
            'feedback-20261029', 'opal-complete-20261030', 'runtime-composites-20261030'))):
        if project == ROOT:
            snapshot = FILES
            proof = evidence
        else:
            snapshot = project / f'ReleasePayload/v{VERSION}'
            shutil.copytree(FILES, snapshot, dirs_exist_ok=True)
            proof = project / evidence.name
            shutil.copyfile(evidence, proof)
        export = read(project / 'TranslateFiles/reports/final_validation.json')
        result = Workflow(project)
        result.record_release_acceptance(dict(
            authorization='explicit_user_release', accept_pending_runtime=True,
            in_game_validation='pending', user_statement='发布吧', game_version=GAME_VERSION, platform=PLATFORM,
            translation_sha256=export['translation_sha256'],
            payload_files=[dict(path=(snapshot / e['archive_path']).relative_to(project).as_posix(),
                                sha256=e['sha256'], game_path=e['path']) for e in payload],
            evidence_files=[dict(path=proof.relative_to(project).as_posix(), sha256=sha(proof))]))
        checks.append(dict(project=project.name, result=result.release_check()))
    write(CONFIG / f'release_checks_v{VERSION}.json', checks)
    common_module().check_custom_quality(ROOT, [(FILES / e['archive_path'], PurePosixPath(e['path'])) for e in payload],
                                         GAME_VERSION, PLATFORM)


if __name__ == '__main__':
    if sys.argv[1:] == ['--authorize']:
        authorize()
    elif not sys.argv[1:]:
        stage()
    else:
        raise SystemExit('Usage: build_v14.py [--authorize]')
