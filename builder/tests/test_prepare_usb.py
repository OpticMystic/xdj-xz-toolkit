import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

from builder import service, usb
from builder.jobs import Job


class PrepareUsbTests(unittest.TestCase):
    def test_rejects_existing_loader_before_network(self):
        with tempfile.TemporaryDirectory() as temporary:
            volume=Path(temporary)
            (volume/'autoexec.bin').write_bytes(b'keep this loader')
            with patch.object(usb,'target_info',return_value={'direct_usb_root':True}), \
                 patch.object(service,'official_firmware') as download:
                with self.assertRaisesRegex(FileExistsError,'already has autoexec.bin'):
                    service.dispatch({'method':'prepare_usb','volume':str(volume),'experimental':True},Job())
                download.assert_not_called()
            self.assertEqual((volume/'autoexec.bin').read_bytes(),b'keep this loader')

    def test_requires_fat_usb_root_before_network(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(usb,'target_info',return_value={'direct_usb_root':False}), \
                 patch.object(service,'official_firmware') as download:
                with self.assertRaisesRegex(ValueError,'FAT/FAT32 USB drive'):
                    service.dispatch({'method':'prepare_usb','volume':temporary,'experimental':True},Job())
                download.assert_not_called()

    def test_one_job_downloads_then_builds(self):
        with tempfile.TemporaryDirectory() as temporary:
            volume=Path(temporary)
            with patch.object(usb,'target_info',return_value={'direct_usb_root':True}), \
                 patch.object(service,'official_firmware',return_value=volume/'firmware.zip') as download, \
                 patch('builder.boot_support.ensure_boot_key',return_value=volume/'boot.key') as support, \
                 patch.object(service,'resources',return_value=volume/'resources'), \
                 patch.object(usb,'build_usb',return_value={'image':str(volume/'autoexec.bin')}) as build:
                result=service.dispatch({'method':'prepare_usb','volume':str(volume),'experimental':True},Job())
            download.assert_called_once()
            support.assert_called_once()
            self.assertEqual(build.call_args.args[:3],(volume,volume/'firmware.zip',volume/'boot.key'))
            self.assertTrue(result['one_step'])
            self.assertEqual(result['firmware_source'],'AlphaTheta')


if __name__=='__main__':unittest.main()
