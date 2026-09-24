import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'vendor'))
from tools.xz_cli import latest_runtime_session
from tools.xz_firmware.mods_bundle import load_mods_bundle, verify_mods_source


class BundleTests(unittest.TestCase):
    def test_stale_native_source_and_bootstrap_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            toolkit = Path(folder)/'toolkit'
            bundle = Path(folder)/'bundle'
            bundle.mkdir()
            source = toolkit/'mods/runtime.c'
            source.parent.mkdir(parents=True)
            source.write_bytes(b'int runtime_version = 2;\n')
            bootstrap = toolkit/'vendor/tools/xz_runtime/orchestrator.sh'
            bootstrap.parent.mkdir(parents=True)
            bootstrap.write_bytes(b'#!/bin/sh\necho loader\n')
            (bundle/'bootstrap.sh').write_bytes(bootstrap.read_bytes())
            for name in ('libxz-mods.so', 'libxz-receiver.so'):
                (bundle/name).write_bytes(name.encode())
            with zipfile.ZipFile(bundle/'source.zip', 'w') as archive:
                archive.write(source, 'mods/runtime.c')
            files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in bundle.iterdir()}
            (bundle/'manifest.json').write_text(json.dumps({'schema_version': 1, 'firmware': 'XDJ-XZ 1.26', 'profile': 'experimental', 'files': files}))
            verify_mods_source(bundle, toolkit)
            source.write_bytes(b'int runtime_version = 3;\n')
            with self.assertRaisesRegex(ValueError, 'stale for current source'):
                verify_mods_source(bundle, toolkit)
            source.write_bytes(b'int runtime_version = 2;\n')
            bootstrap.write_bytes(b'#!/bin/sh\necho different\n')
            with self.assertRaisesRegex(ValueError, 'bootstrap is stale'):
                verify_mods_source(bundle, toolkit)

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
