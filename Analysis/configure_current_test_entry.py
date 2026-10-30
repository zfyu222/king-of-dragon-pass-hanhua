"""Point both historic and current local test shortcuts at one checked entry."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    source=ROOT/'hook/start_current_test.ps1'
    source.write_text(source.read_text(encoding='utf-8-sig'),encoding='utf-8-sig')
    old=ROOT/'Repack/Feedback_20261029/KoDPCNHook/启动测试.cmd'
    backup=ROOT/'Analysis/legacy_start_entry_20261030.txt'
    if not backup.exists():backup.write_bytes(old.read_bytes())
    for entry,relative in [(old,r'..\..\..\开始汉化测试.cmd'),(ROOT/'Repack/LeakFix_20261030/开始测试.cmd',r'..\..\开始汉化测试.cmd')]:
        entry.write_bytes(('@echo off\r\nchcp 65001 >nul\r\ncall "%~dp0'+relative+'"\r\n').encode('utf-8'))
    package=ROOT/'Repack/LeakFix_20261030'
    sums=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(package).as_posix()+'\n' for p in sorted(package.rglob('*')) if p.is_file() and p.name not in ('SHA256SUMS.txt','KoDPCNHook.log'))
    (package/'SHA256SUMS.txt').write_text(sums,encoding='utf-8')
if __name__=='__main__':main()
