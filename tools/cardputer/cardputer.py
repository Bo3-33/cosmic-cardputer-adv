#!/usr/bin/env python3
"""Back up Cardputer firmware/SD and install a verified COSMIC release."""
import argparse
from flash_preserve import prepare_update
import fcntl
import getpass
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
from datetime import datetime
import zipfile
import urllib.request
import urllib.error
import urllib.parse
import tempfile

ROOT = Path(os.environ.get('CARDPUTER_HOME', Path.home() / '.local/share/cosmic-cardputer')).expanduser().resolve()
DEFAULT_REPO = 'Bo3-33/cosmic-cardputer-adv' 


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def newest(directory, version=None):
    releases = []
    for p in directory.glob('cosmic-cardputer-adv-v*-full.bin'):
        match = re.fullmatch(r'cosmic-cardputer-adv-v(\d+(?:\.\d+)*)-full.bin', p.name)
        if match and (version is None or match[1] == version.removeprefix('v')):
            releases.append((tuple(map(int, match[1].split('.'))), p))
    if not releases:
        raise RuntimeError('No matching firmware release found beside this command script.')
    return max(releases)[1]


def verify_image(image):
    kit = image.with_name(image.name.replace('-full.bin', '-kit.zip'))
    if not kit.is_file():
        raise RuntimeError(f'Matching release kit missing: {kit}')
    with zipfile.ZipFile(kit) as archive:
        entries = [n for n in archive.namelist() if n.endswith('/SHA256SUMS')]
        if len(entries) != 1:
            raise RuntimeError('Release checksum manifest is missing or ambiguous.')
        matches = [line.split()[0] for line in archive.read(entries[0]).decode().splitlines()
                   if len(line.split()) == 2 and line.split()[1] == 'cosmic-cardputer-adv-full.bin']
    if len(matches) != 1 or sha256(image) != matches[0]:
        raise RuntimeError('Firmware checksum verification failed. Nothing was flashed.')
    return matches[0]



def normalize_repo(value):
    value = value.strip().rstrip('/')
    if value.startswith('https://github.com/'):
        value = value[len('https://github.com/'):]
    value = value.removesuffix('.git')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9_.-]+', value):
        raise RuntimeError('Use --repo OWNER/REPO or https://github.com/OWNER/REPO.')
    if value.split('/')[1] in ('.', '..'):
        raise RuntimeError('Invalid repository name.')
    return value


def saved_repo():
    config = ROOT / 'cardputer-config.json'
    if not config.is_file():
        return DEFAULT_REPO
    return normalize_repo(json.loads(config.read_text())['repo'])


def save_repo(repo):
    ROOT.mkdir(parents=True, exist_ok=True)
    target = ROOT / 'cardputer-config.json'
    temporary = target.with_suffix('.json.partial')
    temporary.write_text(json.dumps({'repo': normalize_repo(repo)}, indent=2) + '\n')
    temporary.replace(target)


def github_json(url):
    request = urllib.request.Request(url, headers={'Accept': 'application/vnd.github+json', 'User-Agent': 'cardputer-updater'})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read(2 * 1024 * 1024 + 1)
        if len(data) > 2 * 1024 * 1024:
            raise RuntimeError('GitHub release metadata is unexpectedly large.')
        return json.loads(data)
    except urllib.error.HTTPError as error:
        if error.code == 404:
            raise RuntimeError('GitHub repository/release not found. Use a public repository with a published release and firmware asset.') from error
        raise RuntimeError(f'GitHub returned HTTP {error.code}; check network access or API rate limits.') from error


def release_asset(release):
    if release.get('draft'):
        raise RuntimeError('Draft firmware releases cannot be installed.')
    matches = [a for a in release.get('assets', []) if re.fullmatch(r'cosmic-cardputer-adv-v[0-9]+(?:\.[0-9]+)*-full\.bin', a.get('name', ''))]
    if len(matches) != 1:
        raise RuntimeError('Release must contain exactly one cosmic-cardputer-adv-vVERSION-full.bin asset.')
    asset = matches[0]
    if not isinstance(asset.get('size'), int) or not 0 < asset['size'] <= 8 * 1024 * 1024:
        raise RuntimeError('Firmware asset is empty or larger than the Cardputer flash.')
    return asset


def download_asset(asset, destination, repo, limit):
    url = asset.get('browser_download_url', '')
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != 'https' or parsed.netloc != 'github.com' or not parsed.path.startswith('/' + repo + '/releases/download/'):
        raise RuntimeError('Refusing an unexpected release asset download URL.')
    size = asset.get('size')
    if not isinstance(size, int) or not 0 < size <= limit:
        raise RuntimeError('Release asset has an invalid size.')
    partial = destination.with_suffix(destination.suffix + '.partial')
    count = 0
    try:
        request = urllib.request.Request(url, headers={'User-Agent': 'cardputer-updater'})
        with urllib.request.urlopen(request, timeout=60) as response, partial.open('wb') as out:
            if urllib.parse.urlparse(response.geturl()).scheme != 'https':
                raise RuntimeError('Download redirected away from HTTPS.')
            while True:
                block = response.read(256 * 1024)
                if not block:
                    break
                count += len(block)
                if count > limit or count > size:
                    raise RuntimeError('Downloaded asset exceeds its declared size.')
                out.write(block)
        if count != size:
            raise RuntimeError('Firmware download was incomplete.')
        digest = asset.get('digest')
        if digest is not None:
            if not re.fullmatch(r'sha256:[0-9a-fA-F]{64}', digest) or sha256(partial).lower() != digest.split(':')[1].lower():
                raise RuntimeError('GitHub asset checksum mismatch. Nothing will be flashed.')
        partial.replace(destination)
    finally:
        partial.unlink(missing_ok=True)


def fetch_release(repo, version=None, download=True):
    repo = normalize_repo(repo)
    if version is None:
        releases = github_json(f'https://api.github.com/repos/{repo}/releases?per_page=100')
        published = [r for r in releases if not r.get('draft') and r.get('published_at')]
        if not published:
            raise RuntimeError('The repository has no published firmware releases.')
        def rank(release):
            match = re.fullmatch(r'v?(\d+(?:\.\d+)*)', release.get('tag_name', ''))
            return (bool(match), tuple(map(int, match[1].split('.'))) if match else (), release['published_at'])
        release = max(published, key=rank)
    else:
        suffix = 'tags/' + urllib.parse.quote('v' + version.removeprefix('v'), safe='')
        release = github_json(f'https://api.github.com/repos/{repo}/releases/{suffix}')
    if release.get('prerelease'):
        print('Selected a published development/prerelease build.', flush=True)
    asset = release_asset(release)
    tag = release.get('tag_name', 'unknown')
    print(f'GitHub release: {repo} / {tag} / {asset["name"]}', flush=True)
    digest = asset.get('digest')
    kit_name = asset['name'].replace('-full.bin', '-kit.zip')
    kits = [a for a in release.get('assets', []) if a.get('name') == kit_name]
    if digest is None and len(kits) != 1:
        raise RuntimeError('Release has neither a SHA-256 asset digest nor a matching release kit with SHA256SUMS.')
    if not download:
        return Path(asset['name']), None, {'repo': repo, 'tag': tag, 'asset': asset['name']}
    cache = ROOT / 'github-downloads' / repo.replace('/', '--')
    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='download-', dir=cache) as stage:
        image = Path(stage) / asset['name']
        download_asset(asset, image, repo, 8 * 1024 * 1024)
        if digest is None:
            kit = Path(stage) / kit_name
            download_asset(kits[0], kit, repo, 100 * 1024 * 1024)
            checksum = verify_image(image)
        else:
            checksum = sha256(image)
        target = cache / asset['name']
        image.replace(target)
    return target, checksum, {'repo': repo, 'tag': tag, 'asset': asset['name']}

def setup_tools():
    project = ROOT.parent
    python = project / 'work/Launcher/.pio/core/penv/bin/python'
    package = project / 'work/Launcher/.pio/core/packages/tool-esptoolpy'
    env = os.environ.copy()
    if python.is_file() and package.is_dir():
        env['PYTHONPATH'] = str(package)
        python = str(python)
    else:
        python = sys.executable
    check = subprocess.run([python, '-c', 'import esptool, serial'], env=env, capture_output=True)
    if check.returncode:
        raise RuntimeError('Missing esptool/pyserial. Run tools/cardputer/install.sh first.')
    return python, env


def select_port(python, env, requested):
    if requested:
        return requested
    code = 'import json; from serial.tools import list_ports; print(json.dumps([p.device for p in list_ports.comports() if p.vid == 0x303a]))'
    ports = json.loads(subprocess.check_output([python, '-c', code], env=env, text=True))
    if len(ports) != 1:
        raise RuntimeError('Connect one Cardputer, or specify --port /dev/ttyACM0.')
    return ports[0]


def sd_devices(tree):
    found = []
    for device in tree:
        if device.get('label') == 'BOCARDPUTER' and device.get('fstype') in ('vfat', 'exfat'):
            found.append(device)
        found.extend(sd_devices(device.get('children', [])))
    return found


def select_sd(requested):
    if requested:
        path = Path(requested).expanduser().resolve()
    else:
        data = json.loads(subprocess.check_output(['lsblk', '-J', '-o', 'PATH,LABEL,FSTYPE,MOUNTPOINTS'], text=True))
        cards = sd_devices(data['blockdevices'])
        if len(cards) != 1:
            raise RuntimeError('Insert the BOCARDPUTER SD card, or specify its mount using --sd /media/USER/LABEL.')
        card = cards[0]
        mounts = [p for p in card.get('mountpoints', []) if p]
        if not mounts:
            subprocess.run(['udisksctl', 'mount', '-b', card['path']], check=True)
            mounts = subprocess.check_output(['findmnt', '-rn', '-S', card['path'], '-o', 'TARGET'], text=True).strip().splitlines()
        if len(mounts) != 1:
            raise RuntimeError('Could not determine a unique SD mount. Specify --sd explicitly.')
        path = Path(mounts[0]).resolve()
    if not path.is_dir() or not os.path.ismount(path) or path == Path('/'):
        raise RuntimeError(f'SD path is not a mounted card root: {path}')
    fs = subprocess.check_output(['findmnt', '-rn', '-M', str(path), '-o', 'FSTYPE'], text=True).strip()
    if fs not in ('vfat', 'exfat'):
        raise RuntimeError(f'Refusing to treat {fs!r} as the SD card. Select its FAT/exFAT mount.')
    return path


def run(command, env=None):
    print('$ ' + shlex.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), env=env, check=True)


def main():
    os.umask(0o077)
    p = argparse.ArgumentParser(description=__doc__, epilog='Examples: cardputer --all --fix-permissions | cardputer --backup-sd | cardputer --backup-firmware | cardputer --flash --release 0.5')
    p.add_argument('--backup-firmware', action='store_true', help='Back up all 8 MB of flash')
    p.add_argument('--backup-sd', action='store_true', help='Back up all SD files, including hidden files, to a tar archive')
    p.add_argument('--flash', action='store_true', help='Install firmware after automatically backing up existing flash')
    p.add_argument('--all', action='store_true', help='Back up SD and flash, then install firmware')
    p.add_argument('--release', help='Release version/tag; default: newest published GitHub version (including prereleases)')
    p.add_argument('--repo', help='GitHub OWNER/REPO or URL; remembered after a successful lookup')
    p.add_argument('--local', action='store_true', help='Explicitly use a local build instead of GitHub')
    p.add_argument('--port', help='Serial device; otherwise auto-detect Espressif USB')
    p.add_argument('--sd', help='Mounted SD root; otherwise detect BOCARDPUTER and mount if necessary')
    p.add_argument('--backup-dir', type=Path, default=ROOT / 'backups', help='Parent backup directory')
    p.add_argument('--fix-permissions', action='store_true', help='Use sudo setfacl if serial access is denied')
    p.add_argument('--dry-run', action='store_true', help='Show the plan without mounting, connecting, backing up or flashing')
    p.add_argument('--reset-config', action='store_true', help='Explicit clean install: do not preserve device settings')
    a = p.parse_args()
    flash = a.flash or a.all
    backup_flash = a.backup_firmware or flash
    backup_sd = a.backup_sd or a.all
    if not (flash or backup_flash or backup_sd):
        if a.repo:
            repo = normalize_repo(a.repo)
            fetch_release(repo, a.release, download=False)
            if not a.dry_run:
                save_repo(repo)
            print(f'GitHub source {"would be" if a.dry_run else "is"} set to {repo}. Use cardputer --all to install.')
        else:
            p.print_help()
        return
    image = None
    checksum = None
    origin = None
    if flash:
        if a.local:
            if a.repo:
                p.error('--repo cannot be combined with --local')
            image = newest(ROOT, a.release)
            checksum = verify_image(image)
        else:
            repo = normalize_repo(a.repo) if a.repo else saved_repo()
            if not repo:
                raise RuntimeError('GitHub source not configured. First run: cardputer --repo OWNER/REPO. No local firmware will be flashed automatically.')
            image, checksum, origin = fetch_release(repo, a.release, download=not a.dry_run)
            if a.repo and not a.dry_run:
                save_repo(repo)
    destination = a.backup_dir.expanduser().resolve()
    print('Plan: ' + ', '.join(x for enabled, x in [(backup_sd, 'back up SD files'), (backup_flash, 'back up all 8 MB flash'), (flash, 'flash ' + (image.name if image else ''))] if enabled), flush=True)
    if a.dry_run:
        print(f'Backup parent: {destination}\nPort: {a.port or "auto-detect"}\nSD: {a.sd or "auto-detect BOCARDPUTER"}\nDry run: no device accessed and no backup files created.')
        return
    # Preflight every requested device before starting a long backup.
    sd = select_sd(a.sd) if backup_sd else None
    if sd and (destination == sd or destination.is_relative_to(sd)):
        raise RuntimeError('Backup destination cannot be inside the SD card being backed up.')
    python, env, port = None, None, None
    if backup_flash:
        python, env = setup_tools()
        port = select_port(python, env, a.port)
        if not os.path.exists(port):
            raise RuntimeError(f'Serial device missing: {port}')
        if not os.access(port, os.R_OK | os.W_OK):
            if not a.fix_permissions:
                raise RuntimeError('Serial permission denied. Add --fix-permissions.')
            run(['sudo', 'setfacl', '-m', f'u:{getpass.getuser()}:rw', port])
            if not os.access(port, os.R_OK | os.W_OK):
                raise RuntimeError('Serial access is still denied.')
    # A local lock prevents two invocations of this command running simultaneously.
    ROOT.mkdir(parents=True, exist_ok=True)
    with (ROOT / '.cardputer.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('Another cardputer command is running. Do not run multiple flashers.')
        folder = destination / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        folder.mkdir(parents=True, exist_ok=False)
        metadata = {'firmware_image': str(image) if image else None, 'image_sha256': checksum, 'port': port, 'sd_mount': str(sd) if sd else None, 'backups': {}, 'github_release': origin}
        if sd:
            partial = folder / 'sd-card.tar.partial'
            print(f'Backing up {sd} → {partial}', flush=True)
            run(['tar', '--one-file-system', '-cpf', partial, '-C', sd, '.'])
            # Validate without printing users\' filenames.
            with open(os.devnull, 'w') as silent:
                subprocess.run(['tar', '-tf', str(partial)], stdout=silent, check=True)
            archive = folder / 'sd-card.tar'
            partial.rename(archive)
            metadata['backups']['sd-card.tar'] = sha256(archive)
        prefix = [python, '-u', '-m', 'esptool', '--chip', 'esp32s3', '--port', port, '--baud', '115200', '--before', 'usb-reset', '--after', 'no-reset', '--no-stub', '--verbose'] if backup_flash else None
        if backup_flash:
            partial = folder / 'firmware-original.bin.partial'
            run(prefix + ['read-flash', '0x0', '0x800000', str(partial)], env)
            if partial.stat().st_size != 0x800000:
                raise RuntimeError('Flash backup is incomplete; firmware installation cancelled.')
            archive = folder / 'firmware-original.bin'
            partial.rename(archive)
            metadata['backups']['firmware-original.bin'] = sha256(archive)
        (folder / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
        (folder / 'SHA256SUMS').write_text(''.join(f'{value}  {name}\n' for name, value in metadata['backups'].items()))
        if flash:
            if sha256(image) != checksum:
                raise RuntimeError('Firmware changed during backup; refusing to flash.')
            write_image = image
            if not a.reset_config:
                write_image = folder / 'update-preserving-config.bin'
                kept = prepare_update(image, folder / 'firmware-original.bin', write_image)
                metadata['preserved_partitions'] = kept
                metadata['prepared_image_sha256'] = sha256(write_image)
                (folder / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
                print('Preserving device settings: ' + ', '.join(kept), flush=True)
            run(prefix + ['write-flash', '0x0', str(write_image)], env)
        print(f'Done. Backups: {folder}', flush=True)
        if backup_flash:
            print('Cardputer left in download mode. Disconnect USB and power-cycle it to run firmware.')


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\nStopped. Partial backups are not valid complete backups. If flashing was interrupted, enter G0 download mode and retry.', file=sys.stderr)
        sys.exit(130)
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.SubprocessError, zipfile.BadZipFile) as e:
        print(f'\nStopped: {e}\nNo flash write starts until all requested backups succeed.', file=sys.stderr)
        sys.exit(1)
