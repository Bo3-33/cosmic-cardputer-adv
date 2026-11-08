import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

script = (Path(__file__).resolve().parents[1] / 'cardputer.py')
spec = importlib.util.spec_from_file_location('cardputer_command', script)
m = importlib.util.module_from_spec(spec)
sys.path.insert(0, str(script.parent))
spec.loader.exec_module(m)

class CommandTests(unittest.TestCase):
    def setup_release(self, root):
        image = root / 'cosmic-cardputer-adv-v0.5-full.bin'
        image.write_bytes(b'firmware fixture')
        checksum = m.sha256(image)
        with zipfile.ZipFile(root / 'cosmic-cardputer-adv-v0.5-kit.zip', 'w') as z:
            z.writestr('cosmic-cardputer-adv/SHA256SUMS', checksum + '  cosmic-cardputer-adv-full.bin\n')
        return image

    def test_selection_and_integrity(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t); image = self.setup_release(root)
            (root / 'cosmic-cardputer-adv-v0.3-full.bin').write_bytes(b'old')
            self.assertEqual(m.newest(root), image)
            self.assertEqual(m.verify_image(image), m.sha256(image))
            image.write_bytes(b'modified')
            with self.assertRaises(RuntimeError): m.verify_image(image)

    def test_all_backup_order_and_hidden_files(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t); self.setup_release(root)
            sd = root / 'sd'; sd.mkdir(); (sd / '.hidden').write_text('keep'); (sd / 'music.txt').write_text('song')
            actions = []; real_run = m.run
            def fake_run(cmd, env=None):
                if 'read-flash' in cmd:
                    actions.append('read-flash')
                    with open(cmd[-1], 'wb') as f: f.truncate(0x800000)
                elif 'write-flash' in cmd:
                    actions.append('write-flash')
                    manifests = list((root / 'backups').glob('*/manifest.json'))
                    self.assertEqual(len(manifests), 1)
                    self.assertEqual(len(json.loads(manifests[0].read_text())['backups']), 2)
                else:
                    actions.append('tar'); real_run(cmd, env)
            with patch.object(m, 'ROOT', root), patch.object(m, 'select_sd', return_value=sd), patch.object(m, 'setup_tools', return_value=('python', {})), patch.object(m, 'select_port', return_value='/dev/null'), patch.object(m, 'run', side_effect=fake_run), patch.object(sys, 'argv', ['cardputer', '--all', '--local', '--reset-config']):
                m.main()
            self.assertEqual(actions, ['tar', 'read-flash', 'write-flash'])
            archive = next((root / 'backups').glob('*/sd-card.tar'))
            with tarfile.open(archive) as tar: self.assertIn('./.hidden', tar.getnames())

    def test_default_flash_preserves_settings_from_backup(self):
        from test_flash_preserve import fixture
        with tempfile.TemporaryDirectory() as t:
            root = Path(t); image = self.setup_release(root)
            image.write_bytes(fixture())
            with zipfile.ZipFile(root / 'cosmic-cardputer-adv-v0.5-kit.zip', 'w') as z:
                z.writestr('cosmic-cardputer-adv/SHA256SUMS', m.sha256(image) + '  cosmic-cardputer-adv-full.bin\n')
            old = fixture(); old[0x9000:0xe000] = b'K' * 0x5000
            def fake_run(cmd, env=None):
                if 'read-flash' in cmd:
                    Path(cmd[-1]).write_bytes(old + b'\xff' * (0x800000-len(old)))
                elif 'write-flash' in cmd:
                    prepared = Path(cmd[-1])
                    self.assertNotEqual(prepared, image)
                    self.assertEqual(prepared.read_bytes()[0x9000:0xe000], b'K' * 0x5000)
            with patch.object(m, 'ROOT', root), patch.object(m, 'setup_tools', return_value=('python', {})), patch.object(m, 'select_port', return_value='/dev/null'), patch.object(m, 'run', side_effect=fake_run), patch.object(sys, 'argv', ['cardputer', '--flash', '--local']):
                m.main()

    def test_failed_backup_never_flashes(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t); self.setup_release(root); calls = []
            def fail(cmd, env=None):
                calls.append(cmd)
                with open(cmd[-1], 'wb') as f: f.write(b'partial')
            with patch.object(m, 'ROOT', root), patch.object(m, 'setup_tools', return_value=('python', {})), patch.object(m, 'select_port', return_value='/dev/null'), patch.object(m, 'run', side_effect=fail), patch.object(sys, 'argv', ['cardputer', '--flash', '--local']):
                with self.assertRaises(RuntimeError): m.main()
            self.assertEqual(len(calls), 1)
            self.assertIn('read-flash', calls[0])

    def test_dry_run_does_not_connect_or_create_backup(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t); self.setup_release(root)
            with patch.object(m, 'ROOT', root), patch.object(m, 'select_sd', side_effect=AssertionError('must not mount')), patch.object(m, 'setup_tools', side_effect=AssertionError('must not connect')), patch.object(sys, 'argv', ['cardputer', '--all', '--dry-run', '--local']): m.main()
            self.assertFalse((root / 'backups').exists())

if __name__ == '__main__': unittest.main()
