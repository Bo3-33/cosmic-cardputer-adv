#!/usr/bin/env python3
"""Recover an ESP32-S3 Cardputer USB connection without erasing or flashing it."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime


def esp_ports(ports):
    return [p for p in ports if p.get('vid') == 0x303A]


def find_tools(here):
    project = here.parent
    interpreter = project / 'work/Launcher/.pio/core/penv/bin/python'
    package = project / 'work/Launcher/.pio/core/packages/tool-esptoolpy'
    env = os.environ.copy()
    if interpreter.is_file() and package.is_dir():
        env['PYTHONPATH'] = str(package)
        python = str(interpreter)
    else:
        python = sys.executable
    result = subprocess.run([python, '-c', 'import esptool, serial'], env=env, capture_output=True)
    if result.returncode:
        raise RuntimeError('esptool/pyserial unavailable. Use this script in the original outputs folder, or install esptool in the Python environment running it.')
    return python, env


def query_ports(python, env):
    code = 'import json; from serial.tools import list_ports; print(json.dumps([dict(device=p.device,vid=p.vid,serial=p.serial_number) for p in list_ports.comports()]))'
    return json.loads(subprocess.check_output([python, '-c', code], env=env, text=True))


def command(python, port, reset, stub=False):
    args = [python, '-u', '-m', 'esptool', '--chip', 'esp32s3', '--port', port,
            '--baud', '115200', '--before', reset, '--after', 'no-reset',
            '--connect-attempts', '2', '--verbose']
    if not stub:
        args.append('--no-stub')
    return args


def run_logged(args, env, log, timeout):
    print('\n$ ' + shlex.join(args), flush=True)
    # File-backed output avoids terminal redraw corruption and pipe deadlocks.
    with tempfile.TemporaryDirectory(prefix='cardputer-log-') as capture_dir, \
            open(os.path.join(capture_dir, 'output'), 'w+', encoding='utf-8') as capture, \
            open(os.path.join(capture_dir, 'output'), 'a', encoding='utf-8') as writer:
        child = subprocess.Popen(args, env=env, stdout=writer, stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + timeout
        position = 0
        try:
            while True:
                capture.seek(position)
                chunk = capture.read()
                position = capture.tell()
                if chunk:
                    print(chunk, end='', flush=True)
                    log.write(chunk)
                    log.flush()
                if child.poll() is not None:
                    capture.seek(position)
                    chunk = capture.read()
                    if chunk:
                        print(chunk, end='', flush=True)
                        log.write(chunk)
                    return child.returncode == 0
                if time.monotonic() >= deadline:
                    print('\nAttempt timed out; stopping only this esptool process.', flush=True)
                    child.terminate()
                    try:
                        child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()
                    return False
                time.sleep(.2)
        except BaseException:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Optional explicit port, e.g. /dev/ttyACM0')
    parser.add_argument('--wait', type=int, default=90, help='Seconds to wait for USB (default: 90)')
    parser.add_argument('--fix-permissions', action='store_true', help='Use sudo setfacl to grant your user access to the selected port')
    parser.add_argument('--test-bytes', type=lambda v: int(v, 0), default=0x100000,
                        help='Read-only transfer test size, default 0x100000 (1 MB)')
    args = parser.parse_args()
    if args.wait < 1 or not 4096 <= args.test_bytes <= 0x800000:
        parser.error('Use --wait >= 1 and --test-bytes between 4096 and 0x800000')
    here = Path(__file__).resolve().parent
    python, env = find_tools(here)
    print('This script never erases or writes flash. It may reset the Cardputer.\nClose serial monitors and run only one connection/flashing script at a time.', flush=True)
    print('Waiting for USB. If needed: unplug, switch OFF, hold physical G0 while reconnecting USB, then release G0.', flush=True)
    deadline = time.monotonic() + args.wait
    port = args.port
    serial = None
    while time.monotonic() < deadline:
        ports = query_ports(python, env)
        if args.port:
            if os.path.exists(args.port):
                break
        else:
            candidates = esp_ports(ports)
            if len(candidates) > 1:
                raise RuntimeError('Multiple Espressif devices: choose --port ' + ' or --port '.join(p['device'] for p in candidates))
            if candidates:
                port = candidates[0]['device']
                serial = candidates[0].get('serial')
                break
        time.sleep(1)
    else:
        raise RuntimeError('No Cardputer USB port appeared. A script cannot recover a device that is not enumerating; try a different data cable/direct USB port.')

    def access(selected):
        if os.access(selected, os.R_OK | os.W_OK):
            return
        if not args.fix_permissions:
            raise RuntimeError(f'Permission denied on {selected}. Rerun with --fix-permissions to grant temporary access using sudo.')
        if not shutil.which('setfacl') or not shutil.which('sudo'):
            raise RuntimeError('sudo/setfacl is unavailable. Mounting or SD changes will not fix serial permissions; install the ACL tools or configure dialout access.')
        import getpass
        subprocess.run(['sudo', 'setfacl', '-m', f'u:{getpass.getuser()}:rw', selected], check=True)
        if not os.access(selected, os.R_OK | os.W_OK):
            raise RuntimeError(f'Still unable to access {selected}')

    access(port)
    logs = Path(os.environ.get('CARDPUTER_HOME', Path.home() / '.local/share/cosmic-cardputer')).expanduser() / 'connection-logs'
    logs.mkdir(parents=True, exist_ok=True)
    path = logs / ('cardputer-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.log')
    print(f'Found {port}. Diagnostic log: {path}', flush=True)
    with path.open('w') as log, tempfile.TemporaryDirectory(prefix='cardputer-read-') as temp:
        # ROM first, to avoid the RAM-stub transfer that failed in the original script.
        for reset, stub in [('usb-reset', False), ('no-reset', False), ('default-reset', False), ('usb-reset', True)]:
            if not os.path.exists(port) or not os.access(port, os.R_OK | os.W_OK):
                print('USB changed; checking for the same device...', flush=True)
                for _ in range(15):
                    candidates = esp_ports(query_ports(python, env))
                    if serial:
                        candidates = [p for p in candidates if p.get('serial') == serial]
                    elif args.port:
                        candidates = [p for p in candidates if p['device'] == args.port]
                    if len(candidates) == 1:
                        port = candidates[0]['device']
                        access(port)
                        break
                    time.sleep(1)
                else:
                    raise RuntimeError(f'The USB connection disappeared. See {path}')
            prefix = command(python, port, reset, stub)
            label = f'\nAttempt: {reset}, {"RAM stub" if stub else "ROM loader"}\n'
            print(label, flush=True)
            log.write(label)
            if not run_logged(prefix + ['flash-id'], env, log, 35):
                continue
            test = Path(temp) / 'read-test.bin'
            test.unlink(missing_ok=True)
            read_prefix = command(python, port, 'no-reset', stub)
            if run_logged(read_prefix + ['read-flash', '0x0', hex(args.test_bytes), str(test)], env, log, 240):
                if test.is_file() and test.stat().st_size == args.test_bytes:
                    print(f'\nCONNECTED: identified ESP32-S3 and read {args.test_bytes} bytes successfully.\nPort: {port}\nLoader: {"RAM stub" if stub else "ROM"}; reset: {reset}; baud: 115200\nDevice left in download mode. No firmware was flashed.\nLog: {path}', flush=True)
                    return
            print('\nTransfer failed. Trying the next supported connection method.', flush=True)
    raise RuntimeError(f'Connection attempts failed. See {path}. Try another data cable/direct USB port; do not start several flashers together.')


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\nStopped. No flash writes were requested.', file=sys.stderr)
        sys.exit(130)
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'\nConnection stopped: {error}', file=sys.stderr)
        sys.exit(1)
