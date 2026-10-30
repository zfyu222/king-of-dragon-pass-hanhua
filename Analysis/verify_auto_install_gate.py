"""Run production install gate with fake installation and game-launch endpoints."""
import hashlib,json,subprocess,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    area=ROOT/'Repack'/('自动安装 回放_'+uuid.uuid4().hex[:8]);tools=area/'汉化工具';tools.mkdir(parents=True)
    source=(ROOT/'Dispatch/Files/汉化工具/launch.ps1').read_text(encoding='utf-8-sig')
    # Only replace external process endpoints. Keep real resource checking,
    # current-version decision, exception handling and install dispatch.
    start=source.index('foreach($watcher in');end=source.index("Write-Output ('当前汉化补丁",start)
    source=source[:start]+source[end:]
    source=source[:source.index('Start-Process -WindowStyle Hidden')]+"[IO.File]::WriteAllText((Join-Path $package 'launched.txt'),'fake-game')\n"
    (tools/'launch.ps1').write_text(source,encoding='utf-8-sig')
    stub="param($Action,$GameRoot)\n$ErrorActionPreference='Stop'\nif(Test-Path (Join-Path $GameRoot 'fail.txt')){throw 'expected install failure'}\nAdd-Content -LiteralPath (Join-Path $GameRoot 'installs.txt') -Value 'install'\n[IO.File]::WriteAllText((Join-Path $GameRoot 'data.txt'),'current')\n"
    (tools/'install.ps1').write_text(stub,encoding='utf-8-sig')
    (area/'PATCH_MANIFEST.json').write_text(json.dumps(dict(files=[dict(role='resource',path='data.txt',installed_sha256=hashlib.sha256(b'current').hexdigest())])),encoding='utf-8')
    def run(ok=True):
        p=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(tools/'launch.ps1')],capture_output=True,timeout=15)
        assert (p.returncode==0)==ok,p.stderr
    run();assert (area/'launched.txt').exists()
    before=(area/'data.txt').stat().st_mtime_ns;run();assert len((area/'installs.txt').read_text().splitlines())==1 and before==(area/'data.txt').stat().st_mtime_ns
    (area/'data.txt').write_text('old');run();assert len((area/'installs.txt').read_text().splitlines())==2
    (area/'data.txt').unlink();run();assert len((area/'installs.txt').read_text().splitlines())==3
    (area/'launched.txt').unlink();(area/'data.txt').write_text('old');(area/'fail.txt').touch();run(False);assert not (area/'launched.txt').exists()
    result=dict(status='passed',checks=['first launch installs before fake game','installed launch skips writes','old version installs','missing resource installs','install failure prevents game launch'],real_game_launched=False,scope='production decision branch, isolated install and launch endpoints')
    (ROOT/'Analysis/auto_install_gate_validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':main()
