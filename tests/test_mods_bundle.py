import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'vendor'))
from tools.xz_cli import latest_runtime_session
from tools.xz_firmware.mods_bundle import load_mods_bundle


class BundleTests(unittest.TestCase):
    def test_paired_bundle_and_corrupt_receiver(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            files = {}
            for name in ('libxz-mods.so', 'libxz-receiver.so', 'bootstrap.sh', 'source.zip'):
                data = name.encode()
                (root/name).write_bytes(data)
                files[name] = hashlib.sha256(data).hexdigest()
            manifest = {'schema_version': 1, 'firmware': 'XDJ-XZ 1.26',
                        'profile': 'experimental', 'files': files}
            (root/'manifest.json').write_text(json.dumps(manifest))
            self.assertEqual(load_mods_bundle(root), manifest)
            (root/'libxz-receiver.so').write_bytes(b'different receiver')
            with self.assertRaisesRegex(ValueError, 'integrity check failed'):
                load_mods_bundle(root)

    def test_failed_latest_boot_does_not_reuse_previous_success(self):
        marker = '=== XDJ-XZ Diagnostic Native Loader'
        log = marker+'\nSUCCESS\n'+marker+'\nFAILED'
        self.assertNotIn('SUCCESS', latest_runtime_session(log))
        self.assertIn('FAILED', latest_runtime_session(log))
        self.assertEqual(latest_runtime_session('SUCCESS'), '')


if __name__ == '__main__':
    unittest.main()
