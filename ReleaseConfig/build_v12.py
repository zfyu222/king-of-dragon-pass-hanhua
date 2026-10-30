"""Stage v1.2 from reviewed test payloads, retaining prior public archives."""
import importlib.util,json,shutil,sys
from pathlib import Path,PurePosixPath
from types import SimpleNamespace
from build_release import ROOT,WORKSPACE,sha,write
sys.path.insert(0,str(WORKSPACE/'Tools/localization'))
from translation_workflow import Workflow

def build():
    cfg=json.loads((ROOT/'ReleaseConfig/release.json').read_text(encoding='utf-8'))
    cfg['version']='1.2';write(ROOT/'ReleaseConfig/release.json',cfg)
    if '--resume' in sys.argv:
        out=ROOT/'Dispatch/Files'
        manifest=json.loads((out/'PATCH_MANIFEST.json').read_text(encoding='utf-8'))
        if manifest['archive']!='龙城之王_子非鱼汉化1.2.zip':raise ValueError('Unexpected staging version')
        finalize(out,cfg,manifest)
        return
    source=ROOT/'Repack/LeakFix_20261030'
    manifest=json.loads((source/'PATCH_MANIFEST.json').read_text(encoding='utf-8'))
    out=ROOT/'Dispatch/Files'
    if out.exists():
        previous=json.loads((out/'PATCH_MANIFEST.json').read_text(encoding='utf-8'))
        if previous['archive']!='龙城之王_子非鱼汉化1.1.zip':raise ValueError('Unexpected existing staging')
        retained=ROOT/'Dispatch/Files_v1.1'
        if retained.exists():raise ValueError('Retained staging already exists')
        for p in (out,retained):
            if not p.resolve().is_relative_to((ROOT/'Dispatch').resolve()):raise ValueError('Move outside Dispatch')
        out.rename(retained)
    out.mkdir()
    for entry in manifest['files']:
        src=source/entry['archive_path']
        if sha(src)!=entry['sha256']:raise ValueError('Reviewed test payload changed')
        dst=out/entry['archive_path'];dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
    # Share the tested Toolhelp implementation, removing local-only junction and
    # project paths. The published helper resolves only its own package/GameRoot.
    launch=(ROOT/'hook/start_current_test.ps1').read_text(encoding='utf-8-sig')
    launch=launch.replace('param([switch]$CheckOnly)',"param([string]$GameRoot='', [switch]$CheckOnly)")
    begin=launch.index('$project=Split-Path');end=launch.index('$expected=',begin)
    launch=launch[:begin]+"$package=Split-Path -Parent $PSScriptRoot\nif(-not $GameRoot){$GameRoot=$package}\n$game=(Resolve-Path -LiteralPath $GameRoot).Path.TrimEnd('\\','/')\n"+launch[end:]
    launch=launch.replace("if($watcher.Path -and $watcher.Path.StartsWith($project+'\\',[StringComparison]::OrdinalIgnoreCase))",'if($watcher.Path)')
    launch=launch.replace("-Action launch -GameRoot $game","-Action install -GameRoot $game\nStart-Process -WindowStyle Hidden -FilePath (Join-Path $package '汉化文件/KoDPCNHook/KoDPCNLauncher.exe') -ArgumentList ('\"'+(Join-Path $game 'King of Dragon Pass.exe')+'\"')")
    launch=launch.replace('当前测试包：','当前汉化补丁：').replace('旧测试启动器','旧汉化启动器').replace('新版测试DLL','新版汉化DLL')
    if '$project' in launch or 'StaticHanHua' in launch or 'LeakFix_' in launch:raise ValueError('Local-only path in player helper')
    (ROOT/'ReleaseConfig/launch.ps1').write_text(launch,encoding='utf-8-sig')
    install=(ROOT/'ReleaseConfig/install.ps1').read_text(encoding='utf-8-sig')
    anchor="$GameRoot=(Resolve-Path -LiteralPath $GameRoot).Path.TrimEnd('\\','/')"
    if "'launch.ps1'" not in install:
        install=install.replace(anchor,anchor+"\nif($Action -eq 'launch'){& (Join-Path $PSScriptRoot 'launch.ps1') -GameRoot $GameRoot;return}")
        begin=install.index("if($Action -eq 'launch'){",install.index("Write-Output '《龙城之王》汉化安装完成。'"))
        install=install[:begin]
    (ROOT/'ReleaseConfig/install.ps1').write_text(install,encoding='utf-8-sig')
    auxiliary=[]
    def helper(relative,content,encoding):
        target=out/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(content,encoding=encoding,newline='')
        auxiliary.append(dict(path=relative,archive_path=relative,sha256=sha(target),size=target.stat().st_size))
    for action,name in [('install','安装汉化'),('launch','启动汉化'),('uninstall','卸载汉化')]:
        helper(name+'.cmd','@echo off\r\nchcp 65001 >nul\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0汉化工具\\install.ps1" -Action '+action+'\r\nif errorlevel 1 (pause & exit /b 1)\r\n'+('' if action=='launch' else 'pause\r\n'),'utf-8')
    helper('汉化工具/install.ps1',install,'utf-8-sig');helper('汉化工具/launch.ps1',launch,'utf-8-sig')
    manifest={k:v for k,v in manifest.items() if k not in ('channel','in_game_validation','based_on','runtime_build_sha256')}
    manifest.update(archive='龙城之王_子非鱼汉化1.2.zip',created='2026-10-30',auxiliary=auxiliary)
    write(out/'PATCH_MANIFEST.json',manifest)
    finalize(out,cfg,manifest)

def finalize(out,cfg,manifest,user_statement='可以，发布吧'):
    version=cfg['version']
    auxiliary=manifest['auxiliary']
    launch_path=out/'汉化工具/launch.ps1'
    launch=launch_path.read_text(encoding='utf-8-sig').replace('当前测试包：','当前汉化补丁：').replace('旧测试启动器','旧汉化启动器').replace('新版测试DLL','新版汉化DLL')
    launch_path.write_text(launch,encoding='utf-8-sig')
    (ROOT/'ReleaseConfig/launch.ps1').write_text(launch,encoding='utf-8-sig')
    for entry in auxiliary:
        entry.update(sha256=sha(out/entry['archive_path']),size=(out/entry['archive_path']).stat().st_size)
    write(out/'PATCH_MANIFEST.json',manifest)
    payload=manifest['files']+auxiliary
    evidence=ROOT/f'ReleaseConfig/USER_RELEASE_AUTHORIZATION_v{version}.md'
    evidence.write_text('用户明确发布指令：'+user_statement+'\n\n本记录仅用于当前载荷的发布授权，保持in_game_validation=pending，不代填实机通过；既有文本审校及限制保留。\n',encoding='utf-8')
    checks=[]
    for project in [ROOT,ROOT/'TranslateFiles/feedback-20261029',ROOT/'TranslateFiles/opal-complete-20261030',ROOT/'TranslateFiles/runtime-composites-20261030']:
        if project==ROOT:
            selected=[dict(path=(out/e['archive_path']).relative_to(ROOT).as_posix(),sha256=e['sha256'],game_path=e['path']) for e in payload]
            proof=evidence.relative_to(ROOT).as_posix()
        else:
            snapshot=project/f'ReleasePayload/v{version}'
            shutil.copytree(out,snapshot,dirs_exist_ok=True)
            proof=f'USER_RELEASE_AUTHORIZATION_v{version}.md';shutil.copyfile(evidence,project/proof)
            selected=[dict(path=f'ReleasePayload/v{version}/'+e['archive_path'],sha256=e['sha256'],game_path=e['path']) for e in payload]
        export=json.loads((project/'TranslateFiles/reports/final_validation.json').read_text(encoding='utf-8'))
        flow=Workflow(project)
        report=dict(authorization='explicit_user_release',accept_pending_runtime=True,in_game_validation='pending',user_statement=user_statement,game_version='Steam v2.4.0',platform='Steam（Windows）',translation_sha256=export['translation_sha256'],payload_files=selected,evidence_files=[dict(path=proof,sha256=sha(project/proof))])
        flow.record_release_acceptance(report);checks.append(dict(project=str(project),result=flow.release_check()))
    write(ROOT/f'ReleaseConfig/release_checks_v{version}.json',checks)
    spec=importlib.util.spec_from_file_location('release_common',WORKSPACE/'.agents/skills/game-release-patch/scripts/create_release_patch.py');common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)
    common.check_custom_quality(ROOT,[(out/e['archive_path'],PurePosixPath(e['path'])) for e in payload],'Steam v2.4.0','Steam（Windows）')
    args=SimpleNamespace(title='《龙城之王》子非鱼AI汉化补丁 v1.2',install='解压到 King of Dragon Pass.exe 所在文件夹，双击“启动汉化.cmd”。',upgrade='覆盖解压新版补丁后，运行“安装汉化.cmd”。',uninstall='运行“卸载汉化.cmd”。',introduction='为《King of Dragon Pass（龙城之王）》制作的简体中文汉化补丁。',game_version='Steam v2.4.0',platform='Steam（Windows）',coverage='界面、氏族管理、顾问提示、剧情、地图文字、神话插图页和内置手册。',download_url=cfg['download_url'],mirror_url=cfg['mirror_url'],share_name=cfg['share_name'],feedback_url=cfg['feedback_url'],feedback_name=cfg['feedback_name'],game_name=cfg['game_name'],release_date='2026-10-30',changes='补充遗漏文本；修复战报、史册及事件选项的动态匹配和文本截断；改进启动入口及新版补丁加载检查。')
    args.title=f'《龙城之王》子非鱼AI汉化补丁 v{version}'
    notice=''
    if cfg.get('launch_mode')=='custom_launcher':
        args.install='解压到 King of Dragon Pass.exe 所在文件夹，双击“龙城之王_中文版.exe”，首次运行自动安装并启动，以后直接启动。'
        args.upgrade='覆盖解压新版补丁后，运行“龙城之王_中文版.exe”。'
        args.changes='统一使用“龙城之王_中文版.exe”入口，未安装时自动安装，已安装则直接启动；突出中文版启动说明。'
        notice='【重要：启动中文版】\n请运行“龙城之王_中文版.exe”；直接运行原游戏EXE不能启用本汉化。\n\n'
    guide=common.make_guide(args,version,common.previous_changelog(ROOT/'Dispatch',cfg['game_name'],version))
    if notice:guide=guide.replace('\n安装汉化\n','\n'+notice+'安装汉化\n',1)
    (out/'补丁说明.txt').write_text(guide,encoding='utf-8-sig')
    body=guide.replace(notice,'**'+notice.strip().replace('\n',' ')+'**\n\n',1) if notice else guide
    artwork=json.loads((ROOT/'ReleaseConfig/artwork_status.json').read_text(encoding='utf-8'))
    if artwork['status']=='completed':body='![龙城之王 子非鱼汉化]('+artwork['public_url']+')\n\n'+body
    (ROOT/'ReleaseConfig/release_body.md').write_text(body,encoding='utf-8')
    (out/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(out).as_posix()+'\n' for p in sorted(out.rglob('*')) if p.is_file() and p.name!='SHA256SUMS.txt'),encoding='utf-8')
    print('Staged v1.2; verified formal exports for four projects; installation replay required')
if __name__=='__main__':build()
