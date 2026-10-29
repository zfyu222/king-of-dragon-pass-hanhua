"""Stage a local test package from immutable v1.1 resources plus new runtime."""
import hashlib,importlib.util,json,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    base=ROOT/'Dispatch/Files';native=ROOT/'Repack/Feedback_20261030_Ready/KoDPCNHook'
    package=ROOT/'Repack/LeakFix_20261030'
    if package.exists():
        previous=json.loads((package/'PATCH_MANIFEST.json').read_text(encoding='utf-8'))
        if previous.get('channel')!='local_test_20261030':raise ValueError('Refuse to overwrite unrelated directory')
    package.mkdir(exist_ok=True)
    manifest=json.loads((base/'PATCH_MANIFEST.json').read_text(encoding='utf-8'))
    release_hash=sha(ROOT/'Dispatch/龙城之王_子非鱼汉化1.1.zip')
    for entry in manifest['files']+manifest['auxiliary']:
        source=base/entry['archive_path']
        if sha(source)!=entry['sha256']:raise ValueError('Released staging payload changed')
        if entry.get('role')=='runtime':source=native/Path(entry['archive_path']).name
        dest=package/entry['archive_path'];dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
        entry.update(sha256=sha(dest),size=dest.stat().st_size)
    manifest.update(archive=None,channel='local_test_20261030',in_game_validation='pending',based_on='v1.1',runtime_build_sha256=sha(native/'BUILD_MANIFEST.json'))
    (package/'PATCH_MANIFEST.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    game=(ROOT/'Game').resolve()
    for name,action in [('开始测试.cmd','launch'),('安装测试版.cmd','install'),('恢复英文.cmd','uninstall')]:
        script='@echo off\r\nchcp 65001 >nul\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0汉化工具\\install.ps1" -Action '+action+' -GameRoot "'+str(game)+'"\r\npause\r\n'
        (package/name).write_bytes(script.encode('utf-8'))
    guide='《龙城之王》2026-10-30 漏翻修复测试版\n\n退出游戏后，双击“开始测试.cmd”。脚本自动安装并启动汉化，读取现有存档。\n本测试包使用本机已记录的Steam游戏目录：'+str(game)+'\n需要恢复英文时，退出游戏后双击“恢复英文.cmd”。\n\n本次更新：补齐Opal遗漏字段，支持变量及代词展开、完整战报与史册句式，修复末尾占位符导致后半段文本截断。\n已通过离线匹配与隔离安装验证；游戏内字体、排版与存档场景仍待实测。\n同源多译的219个源键没有按顺序强行覆盖，已单列工程冲突报告；不能保证全游戏零漏翻。\n\n本补丁使用 AI 辅助翻译，请适当降低预期。\n'
    (package/'测试说明.txt').write_text(guide,encoding='utf-8-sig')
    sums=''.join(sha(p)+'  '+p.relative_to(package).as_posix()+'\n' for p in sorted(package.rglob('*')) if p.is_file() and p.name!='SHA256SUMS.txt')
    (package/'SHA256SUMS.txt').write_text(sums,encoding='utf-8')
    spec=importlib.util.spec_from_file_location('fixture_verify',ROOT/'ReleaseConfig/verify_release.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.verify(files=package,report_path=ROOT/'Analysis/feedback_install_validation_20261030.json')
    # Also verify the currently installed v1.1 resources are all accepted inputs.
    for entry in manifest['files']:
        if entry['role']=='resource' and sha(game/entry['path']) not in {entry['original_sha256'],entry['installed_sha256'],*entry['legacy_sha256']}:
            raise ValueError('Current game resource not accepted by test installer: '+entry['path'])
    if sha(ROOT/'Dispatch/龙城之王_子非鱼汉化1.1.zip')!=release_hash:raise ValueError('Public v1.1 changed')
    print('Test package ready: '+str(package))
if __name__=='__main__':main()
