import tempfile
from pathlib import Path
import unittest
from builder import firmware

class FirmwareTests(unittest.TestCase):
    def test_sector_roundtrip_and_iso_members(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);tree=root/'tree'
            runtime=root/'runtime.so';runtime.write_bytes(b'fixture-runtime')
            receiver=root/'receiver.so';receiver.write_bytes(b'fixture-receiver')
            bootstrap=root/'boot.sh';bootstrap.write_bytes(b'#!/bin/sh\necho fixture\n')
            key=bytes(range(32))
            expected=firmware.stage_payload(tree,b'fixture-application',runtime,receiver,bootstrap)
            image=firmware.author_image(tree,key)
            self.assertEqual(len(image)%512,0)
            result=firmware.verify_image(image,key,expected)
            self.assertEqual(result['verified_files'],len(expected))
            with self.assertRaises(ValueError):firmware.verify_image(image,bytes(32),expected)
            with self.assertRaises(ValueError):firmware.verify_image(image,key,{'rbp.patched':b'changed'})
    def test_unknown_firmware_rejected(self):
        with self.assertRaises(ValueError):firmware.patched_application(b'unknown')
    def test_empty_key_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'key';path.write_bytes(b'')
            with self.assertRaises(ValueError):firmware.effective_key(path)

if __name__=='__main__':unittest.main()
