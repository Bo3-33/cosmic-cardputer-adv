import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tempfile
import unittest
import struct
from flash_preserve import prepare_update

def fixture(offset=0x9000, size=0x5000, app=0x240000):
    data=bytearray(b'\xff' * 0x30000)
    entries=[(1,2,offset,size,b'nvs'),(1,0,0xe000,0x2000,b'otadata'),(0,0x20,0x10000,app,b'app0')]
    for i,(kind,sub,start,length,name) in enumerate(entries):
        struct.pack_into('<HBBII16sI',data,0x8000+i*32,0x50aa,kind,sub,start,length,name,0)
    return data

class PreserveTests(unittest.TestCase):
    def run_case(self,old,new):
        t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);root=Path(t.name)
        backup=root/'backup.bin';image=root/'firmware.bin';dest=root/'prepared.bin'
        backup.write_bytes(old+b'\xff'*(0x800000-len(old)));image.write_bytes(new)
        return image,backup,dest
    def test_preserve_nvs_but_install_new_code_table_and_boot_selection(self):
        old=fixture(app=0x200000);old[0x9000:0xe000]=b'S'*0x5000;old[0xe000:0x10000]=b'O'*0x2000
        new=fixture();new[0x10000:]=b'N'*(len(new)-0x10000)
        image,backup,dest=self.run_case(old,new)
        self.assertEqual(prepare_update(image,backup,dest),['nvs'])
        result=dest.read_bytes();self.assertEqual(result[0x9000:0xe000],b'S'*0x5000)
        self.assertEqual(result[0xe000:],new[0xe000:]);self.assertEqual(result[0x8000:0x9000],new[0x8000:0x9000])
        self.assertEqual(image.read_bytes(),new);self.assertEqual(dest.stat().st_mode&0o777,0o600)
    def test_truncation_invalid_table_and_incompatible_shrink_never_create_update(self):
        for old,new in [(fixture(),fixture(size=0x4000)),(bytearray(0x30000),fixture()),(fixture(),bytearray(20))]:
            image,backup,dest=self.run_case(old,new)
            with self.assertRaises(RuntimeError):prepare_update(image,backup,dest)
            self.assertFalse(dest.exists())
    def test_full_backup_required(self):
        image,backup,dest=self.run_case(fixture(),fixture());backup.write_bytes(b'short')
        with self.assertRaises(RuntimeError):prepare_update(image,backup,dest)
        self.assertFalse(dest.exists())
if __name__=='__main__':unittest.main()
