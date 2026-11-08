import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('cardputer', str(Path(__file__).resolve().parents[1] / 'cardputer.py'))
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
DATA = b'verified firmware fixture'
NAME = 'cosmic-cardputer-adv-v9.1-full.bin'
DIGEST = hashlib.sha256(DATA).hexdigest()

class Reply(io.BytesIO):
    def geturl(self): return 'https://release-assets.githubusercontent.com/fixture'

class GithubTests(unittest.TestCase):
    def release(self, digest='sha256:' + DIGEST):
        return {'tag_name':'v9.1', 'draft':False, 'prerelease':False, 'published_at':'2026-11-08T16:00:00Z', 'assets':[
            {'name': NAME, 'size':len(DATA), 'digest':digest,
             'browser_download_url':'https://github.com/test/firmware/releases/download/v9.1/' + NAME}]}

    def test_repo_validation(self):
        self.assertEqual(m.normalize_repo('https://github.com/test/firmware.git'), 'test/firmware')
        for value in ['https://evil.example/test/firmware','test/firmware/releases', '../firmware', 'test/..']:
            with self.assertRaises(RuntimeError): m.normalize_repo(value)

    def test_latest_download_and_checksum(self):
        with tempfile.TemporaryDirectory() as t, patch.object(m, 'ROOT', Path(t)), patch.object(m, 'github_json', return_value=[self.release()]) as metadata, patch.object(m.urllib.request, 'urlopen', return_value=Reply(DATA)):
            path, checksum, origin = m.fetch_release('test/firmware')
            metadata.assert_called_once_with('https://api.github.com/repos/test/firmware/releases?per_page=100')
            self.assertEqual(path.read_bytes(), DATA)
            self.assertEqual(checksum, DIGEST)
            self.assertEqual(origin['tag'], 'v9.1')

    def test_failed_checksum_does_not_publish_download(self):
        with tempfile.TemporaryDirectory() as t, patch.object(m, 'ROOT', Path(t)), patch.object(m, 'github_json', return_value=[self.release('sha256:' + '0'*64)]), patch.object(m.urllib.request, 'urlopen', return_value=Reply(DATA)):
            with self.assertRaises(RuntimeError): m.fetch_release('test/firmware')
            self.assertEqual(list(Path(t).rglob('*.bin')), [])
            self.assertEqual(list(Path(t).rglob('*.partial')), [])

    def test_no_checksums_and_no_kit_rejected(self):
        with patch.object(m, 'github_json', return_value=[self.release(None)]):
            with self.assertRaises(RuntimeError): m.fetch_release('test/firmware', download=False)

    def test_ambiguous_or_draft_rejected(self):
        r=self.release();r['draft']=True
        with self.assertRaises(RuntimeError): m.release_asset(r)
        r=self.release();r['assets']*=2
        with self.assertRaises(RuntimeError): m.release_asset(r)

    def test_dry_run_does_not_download_or_create_cache(self):
        with tempfile.TemporaryDirectory() as t, patch.object(m, 'ROOT', Path(t)), patch.object(m, 'github_json', return_value=[self.release()]), patch.object(m, 'download_asset', side_effect=AssertionError('must not download')):
            _, checksum, _ = m.fetch_release('test/firmware', download=False)
            self.assertIsNone(checksum)
            self.assertEqual(list(Path(t).iterdir()), [])

    def test_reject_other_host_download(self):
        asset=self.release()['assets'][0];asset['browser_download_url']='https://other.example/firmware.bin'
        with self.assertRaises(RuntimeError): m.download_asset(asset, Path('/tmp/unused-cardputer-test.bin'), 'test/firmware', 8388608)

if __name__ == '__main__': unittest.main()
