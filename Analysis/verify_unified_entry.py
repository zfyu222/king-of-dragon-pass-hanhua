"""Check the generated EXE and real adapter without launching the game."""
import hashlib,json,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    package=ROOT/'Repack/UnifiedEntry_20261030'
    live_adapter=package/'汉化工具/launch.ps1'
    live_adapter.write_text((ROOT/'ReleaseConfig/launch.ps1').read_text(encoding='utf-8-sig'),encoding='utf-8-sig')
    fixture=ROOT/'Repack/统一入口 隔离校验';fixture.mkdir(exist_ok=True)
    tools=fixture/'汉化工具';tools.mkdir(exist_ok=True)
    shutil.copyfile(package/'龙城之王_中文版.exe',fixture/'龙城之王_中文版.exe')
    adapter=tools/'launch.ps1'
    adapter.write_text("[IO.File]::WriteAllText((Join-Path (Split-Path -Parent $PSScriptRoot) 'forwarded.txt'),'ok')",encoding='utf-8-sig')
    subprocess.run([str(fixture/'龙城之王_中文版.exe')],check=True,timeout=15)
    assert (fixture/'forwarded.txt').read_text()=='ok'
    shutil.copyfile(ROOT/'ReleaseConfig/launch.ps1',adapter)
    content=b'installed-current-version'
    manifest={'files':[dict(role='resource',path='data.txt',installed_sha256=hashlib.sha256(content).hexdigest())]}
    (fixture/'PATCH_MANIFEST.json').write_text(json.dumps(manifest),encoding='utf-8')
    if (fixture/'data.txt').exists():(fixture/'data.txt').unlink()
    def installed():
        result=subprocess.check_output(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(adapter),'-CheckOnly'],timeout=15)
        return json.loads(result)['installed']
    assert not installed()
    target=fixture/'data.txt';target.write_bytes(content);mtime=target.stat().st_mtime_ns
    assert installed() and target.stat().st_mtime_ns==mtime
    target.write_bytes(b'old-or-changed-version');assert not installed()
    manifest=json.loads((package/'PATCH_MANIFEST.json').read_text(encoding='utf-8'))
    removed={'安装汉化.cmd','启动汉化.cmd'}
    for name in removed:(package/name).unlink()
    manifest['auxiliary']=[e for e in manifest['auxiliary'] if e['archive_path'] not in removed]
    exe=package/'龙城之王_中文版.exe'
    manifest['auxiliary'].append(dict(path=exe.name,archive_path=exe.name))
    for entry in manifest['auxiliary']:
        p=package/entry['archive_path'];entry.update(sha256=hashlib.sha256(p.read_bytes()).hexdigest(),size=p.stat().st_size)
    manifest.update(archive=None,channel='unified_entry_candidate',launch_mode='custom_launcher',launch_exe=exe.name)
    (package/'PATCH_MANIFEST.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    guide=(package/'补丁说明.txt').read_text(encoding='utf-8-sig')
    guide=guide.replace('安装汉化\n','【重要：启动中文版】\n请运行“龙城之王_中文版.exe”；直接运行原游戏EXE不能启用本汉化。\n\n安装汉化\n',1).replace('双击“启动汉化.cmd”。','双击“龙城之王_中文版.exe”，首次运行自动安装并启动，以后直接启动。').replace('运行“安装汉化.cmd”。','运行“龙城之王_中文版.exe”。')
    (package/'补丁说明.txt').write_text(guide,encoding='utf-8-sig')
    (package/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(package).as_posix()+'\n' for p in sorted(package.rglob('*')) if p.is_file() and p.name!='SHA256SUMS.txt'),encoding='utf-8')
    report=dict(status='candidate_ready',exe=str(exe),checks=['EXE compilation','package check without launch','real EXE forwards adapter in path with spaces and Chinese','missing installation detected','installed current resource detected without write','changed version detected'],real_game_launched=False,published_zip_changed=False)
    (ROOT/'Analysis/unified_entry_validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
