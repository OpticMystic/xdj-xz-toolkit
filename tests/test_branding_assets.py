import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class BrandingAssetsTests(unittest.TestCase):
    def test_posix_checksum_manifest_resolves_every_file(self):
        spec=importlib.util.spec_from_file_location('build_branding_assets',ROOT/'mods/ui/build_branding_assets.py')
        module=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)
            module.build(ROOT,output)
            manifest=(output/'MD5SUMS').read_bytes()
            self.assertNotIn(b'\r',manifest,'BusyBox treats CR as part of the filename')
            for line in manifest.split(b'\n'):
                if not line:continue
                digest,name=line.split(b'  ',1)
                self.assertEqual(hashlib.md5((output/name.decode('ascii')).read_bytes()).hexdigest(),digest.decode('ascii'))
            self.assertNotIn(b'\r',(output/'apply.sh').read_bytes())

if __name__=='__main__':unittest.main()
