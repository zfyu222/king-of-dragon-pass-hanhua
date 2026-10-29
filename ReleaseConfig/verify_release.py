"""Replay release installation, legacy upgrade, restore and rejection in fixtures."""
import hashlib
import json
import shutil
import subprocess
import uuid
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
FILES=ROOT/'Dispatch/Files'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def verify(files=None,report_path=None):
    FILES=Path(files) if files else ROOT/'Dispatch/Files'
    manifest=json.loads((FILES/'PATCH_MANIFEST.json').read_text(encoding='utf-8'))
    resources=[e for e in manifest['files'] if e['role']=='resource']
    live={e['path']:sha(ROOT/'Game'/e['path']) for e in resources}
    area=ROOT/'Repack'/('release_validation_'+uuid.uuid4().hex[:8]);area.mkdir(parents=True)
    checks=[]
    def fixture(name,legacy=False):
        game=area/name;shutil.copytree(FILES,game)
        shutil.copyfile(ROOT/'Game/King of Dragon Pass.s86',game/'King of Dragon Pass.s86')
        for e in resources:
            rel=e['path']
            original=ROOT/'release/KoDPCNHook/backup/html_original'/rel if rel.endswith('.html') else ROOT/'Repack/Feedback_20261029/KoDPCNHook/backup/before_test'/rel
            if sha(original)!=e['original_sha256']:raise ValueError('Unverified original fixture: '+rel)
            target=game/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(original,target)
            if legacy:
                old=ROOT/'Repack/Feedback_20261029/KoDPCNHook/backup/before_test'/rel
                shutil.copyfile(old,target)
        return game
    def run(game,action,label,success=True):
        r=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(game/'汉化工具/install.ps1'),'-Action',action,'-GameRoot',str(game),'-SteamRequestFile',str(game/'steam-request.txt')],capture_output=True)
        (area/(label+'.log')).write_bytes(r.stdout+b'\n'+r.stderr)
        if (r.returncode==0)!=success:raise RuntimeError(label+' failed; see '+str(area))
        checks.append(dict(check=label,exit_code=r.returncode,expected_success=success))
    def hashes(game,field):
        for e in resources:
            if sha(game/e['path'])!=e[field]:raise ValueError('Resource mismatch: '+e['path'])
    fresh=fixture('fresh')
    run(fresh,'install','fresh_install');hashes(fresh,'installed_sha256')
    run(fresh,'install','repeat_install');hashes(fresh,'installed_sha256')
    run(fresh,'uninstall','fresh_uninstall');hashes(fresh,'original_sha256')
    payload=fresh/resources[0]['archive_path'];old=payload.read_bytes();payload.write_bytes(old+b'tampered')
    run(fresh,'install','reject_corrupt_payload',False);hashes(fresh,'original_sha256');payload.write_bytes(old)
    target=fresh/resources[0]['path'];original=target.read_bytes();target.write_bytes(original+b'unknown modification')
    before={e['path']:sha(fresh/e['path']) for e in resources}
    run(fresh,'install','reject_unknown_game_content',False)
    assert before=={e['path']:sha(fresh/e['path']) for e in resources}
    legacy=fixture('legacy',True)
    run(legacy,'install','legacy_upgrade');hashes(legacy,'installed_sha256')
    run(legacy,'install','legacy_repeat');hashes(legacy,'installed_sha256')
    run(legacy,'uninstall','legacy_uninstall_steam_restore')
    assert (legacy/'steam-request.txt').read_text()=='steam://validate/352220'
    assert live=={e['path']:sha(ROOT/'Game'/e['path']) for e in resources}
    result=dict(status='passed',resource_targets=len(resources),checks=checks,real_game_untouched=True,
                legacy_steam_uri_verified_without_launching_steam=True,fixture=str(area),manifest_sha256=sha(FILES/'PATCH_MANIFEST.json'))
    (Path(report_path) if report_path else ROOT/'ReleaseConfig/installation_validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=='__main__':verify()
