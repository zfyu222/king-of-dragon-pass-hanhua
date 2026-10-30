"""Release the verified single-EXE entry candidate without changing v1.2."""
import json,shutil
from build_v12 import ROOT,sha,write,finalize
def main():
    out=ROOT/'Dispatch/Files';old=ROOT/'Dispatch/Files_v1.2'
    if json.loads((out/'PATCH_MANIFEST.json').read_text(encoding='utf-8'))['archive']!='龙城之王_子非鱼汉化1.2.zip':raise ValueError('Unexpected previous staging')
    for p in (out,old):
        if not p.resolve().is_relative_to((ROOT/'Dispatch').resolve()):raise ValueError('Staging outside Dispatch')
    if old.exists():raise ValueError('Previous staging already retained')
    candidate=ROOT/'Repack/UnifiedEntry_20261030'
    manifest=json.loads((candidate/'PATCH_MANIFEST.json').read_text(encoding='utf-8'))
    for entry in manifest['files']+manifest['auxiliary']:
        if sha(candidate/entry['archive_path'])!=entry['sha256']:raise ValueError('Candidate payload changed')
    out.rename(old);out.mkdir()
    for entry in manifest['files']+manifest['auxiliary']:
        target=out/entry['archive_path'];target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(candidate/entry['archive_path'],target)
    manifest.pop('channel',None);manifest.update(archive='龙城之王_子非鱼汉化1.3.zip',created='2026-10-30')
    cfg=json.loads((ROOT/'ReleaseConfig/release.json').read_text(encoding='utf-8'));cfg.update(version='1.3',launch_mode='custom_launcher',launch_exe='龙城之王_中文版.exe');write(ROOT/'ReleaseConfig/release.json',cfg)
    finalize(out,cfg,manifest,user_statement='再次发布补丁吧')
if __name__=='__main__':main()
