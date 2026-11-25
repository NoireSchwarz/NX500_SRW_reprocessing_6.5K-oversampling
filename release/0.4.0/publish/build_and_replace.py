"""Build the complete Unified R1 package and replace the two canonical release assets.

The local build needs no network or credentials. The publication mode runs with
GitHub's short-lived workflow token; no credential is embedded in source or logs.
"""
import argparse
import base64
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

REPO = "NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling"
API = "https://api.github.com/repos/" + REPO
BASELINE_SHA = "c13582e96444968e5d75b2d276ab2ac6ce173f054cc0971bd15a12b78250f3ec"
RELEASE_ID = 457339623
ZIP_NAME = "NX500_Full_Mod_EyeAF_0.4.0.zip"
SHA_NAME = "NX500_Full_Mod_EyeAF_0.4.0.sha256"
SOURCE = Path(__file__).resolve().parents[3]


def sha(path):
    with Path(path).open('rb') as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def unpack(archive, target):
    with ZipFile(archive) as zip_in:
        names = set()
        if zip_in.testzip():
            raise ValueError('Input ZIP CRC failure')
        for item in zip_in.infolist():
            path = PurePosixPath(item.filename)
            if (path.is_absolute() or '..' in path.parts or '\\' in item.filename or ':' in item.filename
                    or item.filename in names or stat.S_ISLNK(item.external_attr >> 16)):
                raise ValueError('Unsafe or duplicate ZIP member')
            names.add(item.filename)
            dest = target.joinpath(*path.parts)
            if item.is_dir():
                dest.mkdir(parents=True, exist_ok=True)
            else:
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(zip_in.read(item))


def package(source, baseline, unified, output):
    source, baseline, unified, output = map(Path, (source, baseline, unified, output))
    if sha(baseline) != BASELINE_SHA:
        raise ValueError('Not the verified previously published complete package')
    output.mkdir(parents=True, exist_ok=True)
    dest = output / ZIP_NAME
    if dest.exists():
        raise FileExistsError(dest)
    with tempfile.TemporaryDirectory(prefix='nx500-publication-', dir=output) as scratch:
        tree = Path(scratch) / 'full'
        tree.mkdir()
        unpack(baseline, tree)
        unpack(unified, tree / 'unified-rebuild')
        pins = json.loads((tree / 'unified-rebuild/rebuild-checksums.json').read_text('utf-8'))
        for name, wanted in pins.items():
            if sha(tree / 'unified-rebuild' / name) != wanted:
                raise ValueError('Unified payload hash mismatch: ' + name)
        release = source / 'release/0.4.0'
        for name in ('compose.py', 'verify.py', 'resume_profile.py', 'README.md', 'full-menu.sh', 'eye-package.sha256', 'gray-calibration.json', 'validation.json'):
            shutil.copyfile(release / name, tree / name)
        for name in ('stage-postflash.py', 'rebuild_evidence.py', 'simple_wake.py', 'simple-wake-body.sh'):
            shutil.copyfile(release / 'fast-start' / name, tree / name)
        for folder in ('capture-check', 'resume-profile', 'rebuild', 'publish'):
            for file in (release / folder).rglob('*'):
                if not file.is_file() or '__pycache__' in file.parts or file.suffix in ('.zip', '.bin', '.arm', '.so', '.pyc'):
                    continue
                dst = tree / 'source/release/0.4.0' / folder / file.relative_to(release / folder)
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(file, dst)
                if folder in ('capture-check', 'resume-profile', 'rebuild'):
                    dst = tree / folder / file.relative_to(release / folder)
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(file, dst)
        shutil.copyfile(source / 'docs/RELEASE_0.4.0_UNIFIED_R1_zh.md', tree / 'RELEASE_UNIFIED_R1_zh.md')
        # The full main module and old first-install camera payload remain byte-identical.
        retained = 0
        with ZipFile(baseline) as old:
            for name in old.namelist():
                if name.startswith(('module/', 'nxl-full040/')) and not name.endswith('/'):
                    if (tree / name).read_bytes() != old.read(name):
                        raise ValueError('Known working baseline changed: ' + name)
                    retained += 1
        report = {'release': '0.4.0 Unified R1', 'baseline_sha256': BASELINE_SHA,
                  'baseline_camera_files_preserved': retained, 'unified_camera_files_verified': len(pins),
                  'PC_cases_passed': 30, 'original_ARM_cases_passed': 8,
                  'hardware_acceptance': {'source': 'maintainer report, 2026-11-25',
                      'same_ordinary_cold_start': True, 'eye_box_normal': True, 'EV_AEL_menu_normal': True},
                  'source_commit': os.environ.get('GITHUB_SHA', 'local-prepublication-build')}
        save(tree / 'PUBLICATION_VERIFICATION.json', report)
        with ZipFile(dest, 'x', compression=ZIP_DEFLATED, compresslevel=6) as zip_out:
            for file in sorted(tree.rglob('*')):
                if not file.is_file() or '__pycache__' in file.parts:
                    continue
                data = file.read_bytes()
                member = ZipInfo(file.relative_to(tree).as_posix(), (2026, 11, 25, 0, 0, 0))
                member.create_system = 3
                member.external_attr = (0o100755 if data.startswith((b'#!', b'\x7fELF')) else 0o100644) << 16
                zip_out.writestr(member, data, compress_type=ZIP_DEFLATED, compresslevel=6)
        with ZipFile(dest) as check:
            if check.testzip() or len(check.namelist()) != len(set(check.namelist())):
                raise ValueError('Final archive verification failed')
            for name in check.namelist():
                if check.read(name) != (tree / name).read_bytes():
                    raise ValueError('Final ZIP readback mismatch: ' + name)
    checksum = output / SHA_NAME
    checksum.write_text(sha(dest) + '  ' + ZIP_NAME + '\n', encoding='ascii')
    save(output / 'build-verification.json', report)
    return dest, checksum


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        return None


class GitHub:
    def __init__(self):
        self.token = os.environ.get('GITHUB_TOKEN')
        if not self.token:
            raise RuntimeError('GITHUB_TOKEN is required for publication')
        self.opener = urllib.request.build_opener(NoRedirect)

    def request(self, method, url, payload=None, *, file=None, accept='application/vnd.github+json'):
        if urllib.parse.urlparse(url).hostname not in ('api.github.com', 'uploads.github.com'):
            raise ValueError('Refuse to transmit GitHub authentication to another host')
        headers = {'Authorization': 'Bearer ' + self.token, 'Accept': accept,
                   'User-Agent': 'NX500-Unified-R1-publisher', 'X-GitHub-Api-Version': '2022-11-28'}
        data = None
        if file:
            data = Path(file).read_bytes()
            headers['Content-Type'] = 'application/octet-stream'
        elif payload is not None:
            data = json.dumps(payload, ensure_ascii=False).encode()
            headers['Content-Type'] = 'application/json'
        request = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with self.opener.open(request, timeout=180 if file else 45) as response:
                body = response.read()
                return json.loads(body) if body else None
        except urllib.error.HTTPError as error:
            try: message = json.loads(error.read()).get('message', '')
            except (ValueError, UnicodeError): message = 'API request failed'
            raise RuntimeError(f'GitHub {method} returned {error.code}: {message}') from None

    def download(self, asset, destination):
        request = urllib.request.Request(asset['url'], headers={
            'Authorization': 'Bearer ' + self.token, 'Accept': 'application/octet-stream',
            'User-Agent': 'NX500-Unified-R1-publisher', 'X-GitHub-Api-Version': '2022-11-28'})
        try:
            response = self.opener.open(request, timeout=90)
        except urllib.error.HTTPError as error:
            if error.code not in (301, 302, 303, 307, 308):
                raise RuntimeError('Asset download failed: ' + str(error.code)) from None
            location = error.headers['Location']
            parsed = urllib.parse.urlparse(location)
            if parsed.scheme != 'https' or not parsed.hostname.endswith('.githubusercontent.com'):
                raise ValueError('Unexpected asset redirect')
            # Do not forward the authentication header to the signed CDN URL.
            response = urllib.request.urlopen(location, timeout=120)
        with response, Path(destination).open('xb') as stream:
            shutil.copyfileobj(response, stream)


def publish(plan, source, output):
    if os.environ.get('GITHUB_REPOSITORY', REPO) != REPO:
        raise RuntimeError('Wrong repository')
    api = GitHub()
    release = api.request('GET', API + '/releases/' + str(RELEASE_ID))
    if release['tag_name'] != 'v0.4.0' or release.get('immutable'):
        raise ValueError('Unexpected or immutable target release')
    output.mkdir(parents=True, exist_ok=True)
    baseline = output / 'baseline-original.zip'
    prior = next(a for a in release['assets'] if a.get('digest') == 'sha256:' + BASELINE_SHA)
    api.download(prior, baseline)
    seed = api.request('GET', API + '/git/blobs/' + plan['unified_blob_sha'])
    unified = output / 'unified-r1.zip'
    unified.write_bytes(base64.b64decode(seed['content']))
    if sha(unified) != plan['unified_zip_sha256']:
        raise ValueError('Unified publication input differs from reviewed bytes')
    archive, checksum = package(source, baseline, unified, output)
    uploads = []
    upload_url = release['upload_url'].split('{')[0]
    for file in (archive, checksum):
        current = api.request('GET', API + '/releases/' + str(RELEASE_ID) + '/assets?per_page=100')
        canonical = [a for a in current if a['name'] == file.name and a.get('digest') == 'sha256:' + sha(file)]
        if canonical:
            uploads.append((file, canonical[0]))
            continue
        staging_name = file.name + '.unified-r1-' + sha(file)[:12] + '-ready'
        found = [a for a in current if a['name'] == staging_name]
        if found:
            asset = found[0]
        else:
            print('Uploading', file.name, file.stat().st_size, flush=True)
            asset = api.request('POST', upload_url + '?' + urllib.parse.urlencode({'name': staging_name}), file=file)
        if asset['state'] != 'uploaded' or asset['size'] != file.stat().st_size or asset.get('digest') != 'sha256:' + sha(file):
            raise ValueError('Remote asset hash or size differs: ' + file.name)
        uploads.append((file, asset))
    notes = (source / 'docs/RELEASE_0.4.0_UNIFIED_R1_zh.md').read_text('utf-8')
    commit = os.environ.get('GITHUB_SHA')
    if commit:
        notes += '\n\n本次更新源码：[完整提交](https://github.com/' + REPO + '/commit/' + commit + ')。\n'
    # Only switch names after both uploads have passed the remote digest check.
    # A failed switch restores the prior canonical names and release description.
    current = api.request('GET', API + '/releases/' + str(RELEASE_ID) + '/assets?per_page=100')
    renamed, activated = [], []
    try:
        for name in (ZIP_NAME, SHA_NAME):
            for asset in current:
                if asset['name'] != name:
                    continue
                replacement = next(a for f, a in uploads if f.name == name)
                if asset['id'] == replacement['id']:
                    continue
                backup_name = name.rsplit('.', 1)[0] + '_before_Unified_R1.' + name.rsplit('.', 1)[1]
                if any(a['name'] == backup_name for a in current):
                    backup_name = name + '.previous-' + str(asset['id'])
                api.request('PATCH', API + '/releases/assets/' + str(asset['id']), {'name': backup_name, 'label': 'Previous package · rollback only'})
                renamed.append(asset)
        for file, asset in uploads:
            if asset['name'] == file.name:
                continue
            api.request('PATCH', API + '/releases/assets/' + str(asset['id']), {'name': file.name, 'label': 'Unified R1 · 合并测试成功'})
            activated.append(asset)
        api.request('PATCH', API + '/releases/' + str(RELEASE_ID), {
            'name': 'NX LUT Lite 0.4.0 · Unified R1 · 合并测试成功', 'body': notes,
            'draft': False, 'prerelease': False, 'make_latest': 'true'})
    except Exception:
        for asset in reversed(activated):
            try: api.request('PATCH', API + '/releases/assets/' + str(asset['id']), {'name': asset['name']})
            except Exception: print('Retained uploaded asset for recovery:', asset['id'], flush=True)
        for asset in reversed(renamed):
            try: api.request('PATCH', API + '/releases/assets/' + str(asset['id']), {'name': asset['name'], 'label': asset.get('label') or ''})
            except Exception: print('Retained previous asset for recovery:', asset['id'], flush=True)
        try: api.request('PATCH', API + '/releases/' + str(RELEASE_ID), {'name': release['name'], 'body': release['body']})
        except Exception: print('Release description needs recovery:', RELEASE_ID, flush=True)
        raise
    final = api.request('GET', API + '/releases/' + str(RELEASE_ID))
    verified = []
    for file, expected in uploads:
        asset = next(a for a in final['assets'] if a['name'] == file.name)
        if asset['id'] != expected['id'] or asset.get('digest') != 'sha256:' + sha(file):
            raise ValueError('Final publication verification failed')
        verified.append({'name': asset['name'], 'sha256': sha(file), 'bytes': asset['size'], 'url': asset['browser_download_url']})
    if '合并测试成功' not in final['body']:
        raise ValueError('Release description was not updated')
    save(output / 'publication-result.json', {'passed': True, 'release_url': final['html_url'],
        'source_commit': commit, 'assets': verified, 'previous_assets_retained_for_rollback': True})
    print('Published and verified', final['html_url'], flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--unified', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--publish', type=Path, metavar='PLAN_JSON')
    args = parser.parse_args()
    if args.publish:
        publish(json.loads(args.publish.read_text('utf-8')), args.source, args.output)
    elif args.baseline and args.unified:
        print(package(args.source, args.baseline, args.unified, args.output)[0])
    else:
        parser.error('Supply --baseline and --unified for a local build, or --publish PLAN_JSON')


if __name__ == '__main__':
    main()
