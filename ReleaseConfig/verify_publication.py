"""Verify public stable release metadata and anonymous asset bytes."""
import hashlib,json,subprocess,urllib.request,zipfile
from decimal import Decimal
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def api(path):return json.loads(subprocess.check_output(['gh','api',path]))
def main():
    cfg=json.loads((ROOT/'ReleaseConfig/release.json').read_text(encoding='utf-8'))
    repo=cfg['repository'];version=cfg['version'];tag='v'+version
    release=api('repos/'+repo+'/releases/latest')
    if release['tag_name']!=tag or release['draft'] or release['prerelease']:raise ValueError('Release is not latest stable')
    repository=api('repos/'+repo)
    if repository['private']:raise ValueError('Repository is private')
    if release['body'].replace('\r\n','\n').strip()!=(ROOT/'ReleaseConfig/release_body.md').read_text(encoding='utf-8').replace('\r\n','\n').strip():raise ValueError('Release body differs from installation guide')
    name='King-of-Dragon-Pass_CN_'+version+'.zip'
    asset=next(a for a in release['assets'] if a['name']==name)
    expected=json.loads((ROOT/'ReleaseConfig/zip_validation.json').read_text(encoding='utf-8'))
    request=urllib.request.Request(asset['browser_download_url'],headers={'User-Agent':'StaticHanHua-public-release-verification'})
    digest=hashlib.sha256();count=0
    with urllib.request.urlopen(request,timeout=60) as response:
        while True:
            chunk=response.read(1024*1024)
            if not chunk:break
            digest.update(chunk);count+=len(chunk)
    if digest.hexdigest()!=expected['sha256'] or count!=expected['bytes'] or count!=asset['size']:raise ValueError('Public download mismatch')
    previous=[api('repos/'+repo+'/releases/tags/v1.'+str(minor))['tag_name']
              for minor in range(int((Decimal(version)-1)*10))]
    artwork=json.loads((ROOT/'ReleaseConfig/artwork_status.json').read_text(encoding='utf-8'))
    with zipfile.ZipFile(expected['archive']) as package:
        guide=package.read('补丁说明.txt').decode('utf-8-sig').replace('\r\n','\n').strip()
    body=release['body'].replace('\r\n','\n').strip()
    if artwork['status']=='completed':
        intro, separator, body=body.partition('\n\n')
        if not separator or artwork['public_url'] not in intro:raise ValueError('Release cover is absent')
        with urllib.request.urlopen(artwork['public_url'],timeout=40) as response:
            if hashlib.sha256(response.read()).hexdigest()!=artwork['sha256']:raise ValueError('Public cover hash mismatch')
    import re
    body=re.sub(r'^\*\*(【重要：启动中文版】)(.+)\*\*$',r'\1\n\2',body,flags=re.M)
    if body!=guide:raise ValueError('Release notes do not match the archived player guide')
    branch=repository['default_branch']
    tree=api('repos/'+repo+'/git/trees/'+branch+'?recursive=1')
    if tree['truncated']:raise ValueError('Cannot verify a truncated public repository tree')
    names={entry['path'] for entry in tree['tree']}
    checked_paths=('README.md','hook/build_runtime_payload.py','hook/src/hook.cpp','ReleaseConfig/build_v14.py',
                   'king-of-dragon-pass_text_translated.csv','king-of-dragon-pass_text_catalog.tsv','Dispatch/Artwork/title.jpg')
    if not set(checked_paths)<=names:raise ValueError('A public engineering file is missing')
    base=f'https://raw.githubusercontent.com/{repo}/{branch}/'
    with urllib.request.urlopen(base+'README.md',timeout=40) as response:
        readme=response.read().decode('utf-8')
    if any(name not in readme for name in ('## 开发与构建','hook/build_runtime_payload.py','ReleaseConfig/build_v14.py',
                                           'king-of-dragon-pass_text_catalog.tsv')):
        raise ValueError('The default-branch README does not contain the engineering entry points')
    report=dict(status='published',version=version,chinese_title='龙城之王',release_url=release['html_url'],download_url=asset['browser_download_url'],asset_display_label=asset['label'],sha256=digest.hexdigest(),bytes=count,public_anonymous_download_verified=True,latest_stable=True,previous_release_preserved=True,preserved_tags=previous,artwork_status=artwork['status'],github_asset_name_note='Local ZIP and asset label use the required Chinese title; GitHub asset URL uses an ASCII filename.')
    if artwork['status']=='completed':report['cover_url']=artwork['public_url']
    report.update(default_branch=branch,public_engineering_paths=list(checked_paths),zip_guide_matches_release=True)
    (ROOT/'ReleaseConfig/publication.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
