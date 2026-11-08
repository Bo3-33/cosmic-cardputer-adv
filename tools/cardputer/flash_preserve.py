"""Prepare a private update image preserving compatible ESP32 settings partitions."""
from pathlib import Path
import os
import struct

FLASH_SIZE = 0x800000

def partitions(data):
    if len(data) < 0x9000:
        raise RuntimeError('Missing partition table; no flash write was started.')
    result = []
    for pos in range(0x8000, 0x9000, 32):
        magic = struct.unpack_from('<H', data, pos)[0]
        if magic in (0xffff, 0xebeb):
            break
        if magic != 0x50aa:
            raise RuntimeError('Invalid partition table; use --reset-config only for an intentional clean install.')
        _, kind, subtype, offset, size, label, flags = struct.unpack_from('<HBBII16sI', data, pos)
        label = label.split(b'\0')[0]
        if not size or offset < 0x9000 or offset + size > FLASH_SIZE:
            raise RuntimeError('Invalid partition bounds.')
        if any(offset < p['offset'] + p['size'] and p['offset'] < offset + size for p in result):
            raise RuntimeError('Overlapping partitions; refusing update.')
        result.append(dict(kind=kind, subtype=subtype, offset=offset, size=size, label=label, flags=flags))
    if not result:
        raise RuntimeError('No partition table found. For first installation, explicitly use --reset-config.')
    return result

def prepare_update(image, backup, destination):
    """Copy NVS and compatible internal filesystems; never copy boot selection data."""
    original = Path(backup).read_bytes()
    if len(original) != FLASH_SIZE:
        raise RuntimeError('Full flash backup required before preserving settings.')
    update = bytearray(Path(image).read_bytes())
    if len(update) > FLASH_SIZE:
        raise RuntimeError('Update exceeds flash capacity.')
    old, new = partitions(original), partitions(update)
    preserved = []
    for p in old:
        if p['kind'] != 1 or p['subtype'] not in (2, 4, 0x81, 0x82, 0x83):
            continue
        candidates = [n for n in new if n['kind'] == 1 and n['subtype'] == p['subtype'] and n['label'] == p['label']]
        if len(candidates) != 1:
            raise RuntimeError('Settings/filesystem partition was removed or renamed. Refusing to discard it; use --reset-config for an intentional reset.')
        n = candidates[0]
        same = n['offset'] == p['offset'] and n['size'] == p['size']
        if (p['flags'] != n['flags'] or ((p['flags'] & 1 or p['subtype'] != 2) and not same)
                or n['size'] < p['size']):
            raise RuntimeError('Incompatible settings partition layout; existing backup retained. No flash write started.')
        if n['offset'] >= len(update) and same:
            preserved.append(p['label'].decode('ascii', 'replace'))
            continue  # Flash write does not touch it.
        if n['offset'] + n['size'] > len(update):
            raise RuntimeError('Settings partition extends beyond update image; refusing an unsafe write.')
        update[n['offset']:n['offset'] + n['size']] = original[p['offset']:p['offset'] + p['size']] + b'\xff' * (n['size'] - p['size'])
        preserved.append(p['label'].decode('ascii', 'replace'))
    # Keep recovery/update files private: NVS can contain Wi-Fi and pairing secrets.
    fd = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as target:
        target.write(update)
        target.flush()
        os.fsync(target.fileno())
    return preserved
