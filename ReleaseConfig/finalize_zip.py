import contextlib
import importlib.util
import json
import sys
import zipfile
from pathlib import Path
from build_release import ROOT, WORKSPACE, sha


def main():
    files=ROOT/'Dispatch/Files'
    m=json.loads((files/'PATCH_MANIFEST.json').read_text(encoding='utf-8'))
    validation=json.loads((ROOT/'ReleaseConfig/installation_validation.json').read_text(encoding='utf-8'))
    if validation['status']!='passed' or validation['manifest_sha256']!=sha(files/'PATCH_MANIFEST.json'):
        raise ValueError('Current installation replay required')
    spec=importlib.util.spec_from_file_location('release_common',WORKSPACE/'.agents/skills/game-release-patch/scripts/create_release_patch.py')
    common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)
    c=json.loads((ROOT/'ReleaseConfig/release.json').read_text(encoding='utf-8'))
    sys.argv=['create_release_patch.py','--project',str(ROOT),'--game-name','龙城之王','--version','1.1',
        '--introduction','龙城之王汉化补丁','--coverage','界面与剧情等','--game-version','Steam v2.4.0','--platform','Steam（Windows）',
        '--install','解压后运行启动汉化.cmd。','--uninstall','运行卸载汉化.cmd。','--download-url',c['download_url'],'--changes','更新词库与标题。','--dry-run']
    payload=m['files']+m['auxiliary']
    for e in payload:
        sys.argv+=['--include',str(files/e['archive_path'])+'='+e['path']]
    with (ROOT/'ReleaseConfig/dry_run.txt').open('w',encoding='utf-8') as out,contextlib.redirect_stdout(out):
        if common.main()!=0:raise ValueError('Dry run failed')
    common.check_custom_quality(ROOT,[(files/e['archive_path'],__import__('pathlib').PurePosixPath(e['path'])) for e in payload],'Steam v2.4.0','Steam（Windows）')
    selected={e['archive_path'] for e in payload}|{'PATCH_MANIFEST.json','SHA256SUMS.txt','补丁说明.txt'}
    for e in payload:
        if sha(files/e['archive_path'])!=e['sha256']:raise ValueError('Payload hash changed')
        if any(x.lower() in ('game','backup','before_test') for x in Path(e['archive_path']).parts):raise ValueError('Forbidden archive path')
    archive=ROOT/'Dispatch'/m['archive']
    if archive.exists():raise ValueError('Do not overwrite an existing release')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for name in sorted(selected):z.write(files/name,name)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert set(z.namelist())==selected
        import hashlib
        for e in payload:assert hashlib.sha256(z.read(e['archive_path'])).hexdigest()==e['sha256']
        assert z.read('补丁说明.txt').startswith(b'\xef\xbb\xbf')
    report=dict(status='passed',archive=str(archive),sha256=sha(archive),bytes=archive.stat().st_size,
                payload_files=len(payload),zip_files=len(selected),crc_passed=True,all_manifest_hashes_passed=True,
                originals_backups_game_binaries_excluded=True)
    (ROOT/'ReleaseConfig/zip_validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
