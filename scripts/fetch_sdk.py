"""Fetch a build-only SDK asset from an unpublished release. Never publish it."""
import hashlib
import json
import os
import pathlib
import re
import urllib.parse
import urllib.request
import zipfile

SDK_SHA256 = 'd6f3c8c86e211e4ceedd3434d0d82d7d9b394fa0eb06c402aeb5eae90a48e65c'


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        target = urllib.parse.urlparse(newurl)
        if target.scheme != 'https':
            raise RuntimeError('SDK redirects must use HTTPS')
        redirected = super().redirect_request(req, fp, code, msg, headers, newurl)
        if target.netloc != urllib.parse.urlparse(req.full_url).netloc:
            redirected.remove_header('Authorization')
        return redirected


def fetch():
    repo = os.environ['GITHUB_REPOSITORY']
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo):
        raise RuntimeError('Invalid repository name')
    token = os.environ['GITHUB_TOKEN']
    opener = urllib.request.build_opener(SafeRedirect())
    headers = {'Authorization': f'Bearer {token}', 'Accept': 'application/vnd.github+json',
               'User-Agent': 'IllustratorTableTool-Build'}
    base = f'https://api.github.com/repos/{repo}'
    request = urllib.request.Request(base + '/releases?per_page=100', headers=headers)
    with opener.open(request, timeout=60) as response:
        releases = json.load(response)
    matches = [r for r in releases if r.get('draft') and r.get('tag_name') == 'sdk-2024-build-input']
    if len(matches) != 1:
        raise RuntimeError('Missing unique unpublished sdk-2024-build-input release. Do not publish the SDK.')
    assets = [a for a in matches[0]['assets'] if a['name'] == 'AI_2024_SDK_Win.zip' and a['state'] == 'uploaded']
    if len(assets) != 1:
        raise RuntimeError('The unpublished draft is missing AI_2024_SDK_Win.zip')
    root = pathlib.Path('.local/sdk').resolve()
    root.mkdir(parents=True, exist_ok=True)
    archive = root.parent / 'sdk-build-input.zip'
    headers['Accept'] = 'application/octet-stream'
    request = urllib.request.Request(base + f"/releases/assets/{int(assets[0]['id'])}", headers=headers)
    digest = hashlib.sha256()
    with opener.open(request, timeout=60) as response, archive.open('wb') as output:
        while chunk := response.read(1024 * 1024):
            digest.update(chunk)
            output.write(chunk)
    if digest.hexdigest() != SDK_SHA256:
        archive.unlink()
        raise RuntimeError('Official SDK SHA-256 does not match the verified input')
    with zipfile.ZipFile(archive) as bundle:
        for item in bundle.infolist():
            if not (root / item.filename).resolve().is_relative_to(root):
                raise RuntimeError('SDK ZIP contains an unsafe path')
        bundle.extractall(root)
    if not (root / 'Adobe Illustrator 2024 SDK/illustratorapi/illustrator/AIPlugin.h').is_file():
        raise RuntimeError('SDK extraction did not produce the expected 2024 headers')
    print('Verified official Illustrator 2024 SDK and extracted build input.')


if __name__ == '__main__':
    fetch()
