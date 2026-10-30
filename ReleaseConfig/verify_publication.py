"""Verify public stable release metadata and anonymous asset bytes."""
import hashlib,json,subprocess,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def api(path):return json.loads(subprocess.check_output(['gh','api',path]))
def main():
    cfg=json.loads((ROOT/'ReleaseConfig/release.json').read_text(encoding='utf-8'))
    repo=cfg['repository'];version=cfg['version'];tag='v'+version
    release=api('repos/'+repo+'/releases/latest')
    if release['tag_name']!=tag or release['draft'] or release['prerelease']:raise ValueError('Release is not latest stable')
    if api('repos/'+repo)['private']:raise ValueError('Repository is private')
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
    previous=[api('repos/'+repo+'/releases/tags/'+t)['tag_name'] for t in ('v1.0','v1.1')]
    artwork=json.loads((ROOT/'ReleaseConfig/artwork_status.json').read_text(encoding='utf-8'))
    report=dict(status='published',version=version,chinese_title='龙城之王',release_url=release['html_url'],download_url=asset['browser_download_url'],asset_display_label=asset['label'],sha256=digest.hexdigest(),bytes=count,public_anonymous_download_verified=True,latest_stable=True,previous_release_preserved=True,preserved_tags=previous,artwork_status=artwork['status'],github_asset_name_note='Local ZIP and asset label use the required Chinese title; GitHub asset URL uses an ASCII filename.')
    if artwork['status']=='completed':report['cover_url']=artwork['public_url']
    (ROOT/'ReleaseConfig/publication.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
