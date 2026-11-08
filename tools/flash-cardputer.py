#!/usr/bin/env python3
"""Back up and flash COSMIC v0.8 on an ESP32-S3 Cardputer ADV."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime
sys.path.insert(0, str(Path(__file__).resolve().parent / "cardputer"))
from flash_preserve import prepare_update

IMAGE = 'cosmic-cardputer-adv-v0.8-full.bin'
EXPECTED_SHA256 = 'bf672df7029e8f8839e1fabc0e4af9f7bf65589ca77136b159f05723a12b888f'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def choose_port(ports):
    matches = [p['device'] for p in ports if p.get('vid') == 0x303A]
    if len(matches) != 1:
        raise RuntimeError('No unique Espressif USB device found. Connect the ADV, then use --port /dev/ttyACM0 if necessary.')
    return matches[0]


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Serial device, e.g. /dev/ttyACM0; otherwise auto-detect Espressif USB')
    parser.add_argument('--backup-dir', type=Path, help='Backup destination; defaults to backups beside this script')
    parser.add_argument('--reset-config', action='store_true', help='Explicit clean install without preserving settings')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    firmware = here / IMAGE
    if not firmware.is_file():
        raise RuntimeError(f'Put {IMAGE} beside this script: {here}')
    if digest(firmware) != EXPECTED_SHA256:
        raise RuntimeError('Firmware checksum mismatch. Download the original v0.8 image again; nothing was flashed.')

    env = os.environ.copy()
    project = here.parent
    bundled_python = project / 'work/Launcher/.pio/core/penv/bin/python'
    bundled_tools = project / 'work/Launcher/.pio/core/packages/tool-esptoolpy'
    if bundled_python.is_file() and bundled_tools.is_dir():
        python = str(bundled_python)
        env['PYTHONPATH'] = str(bundled_tools)
    else:
        python = sys.executable
    check = subprocess.run([python, '-c', 'import esptool, serial'], env=env, capture_output=True)
    if check.returncode:
        raise RuntimeError('esptool is missing. Run this script from the original outputs folder, or install it in a Python virtual environment: python -m pip install esptool')

    port = args.port
    if not port:
        query = 'import json; from serial.tools import list_ports; print(json.dumps([dict(device=p.device,vid=p.vid) for p in list_ports.comports()]))'
        found = subprocess.check_output([python, '-c', query], env=env, text=True)
        port = choose_port(json.loads(found))
    prefix = [python, '-m', 'esptool', '--chip', 'esp32s3', '--port', port]
    directory = (args.backup_dir or here / 'backups').resolve()
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup = directory / f'cardputer-before-v0.8-{stamp}.bin'
    partial = backup.with_suffix('.bin.partial')
    print(f'Firmware verified. Port: {port}', flush=True)
    print(f'Backing up all 8 MB to {backup}', flush=True)
    subprocess.run(prefix + ['read-flash', '0x0', '0x800000', str(partial)], env=env, check=True)
    if partial.stat().st_size != 8 * 1024 * 1024:
        raise RuntimeError(f'Incomplete backup: {partial}. Nothing was flashed.')
    partial.rename(backup)
    backup.with_suffix('.bin.sha256').write_text(f'{digest(backup)}  {backup.name}\n')
    print('Backup complete. Installing COSMIC v0.8 at 0x0...', flush=True)
    write_image = firmware
    if not args.reset_config:
        write_image = backup.with_suffix('.update.bin')
        kept = prepare_update(firmware, backup, write_image)
        print('Preserving device settings: ' + ', '.join(kept), flush=True)
    subprocess.run(prefix + ['write-flash', '0x0', str(write_image)], env=env, check=True)
    print(f'Finished. Backup: {backup}\nRestart the Cardputer if it does not restart automatically.')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError, ValueError) as exc:
        print(f'\nInstallation stopped: {exc}', file=sys.stderr)
        print('If USB will not connect: unplug USB, switch OFF, hold G0 while reconnecting USB, then release G0 and retry. Close serial monitors first.', file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print('\nInterrupted. If flashing had started, reconnect in G0 download mode and rerun the script.', file=sys.stderr)
        sys.exit(130)
