import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from builder.branding import import_branding
from builder.jobs import Job, Cancelled


class BrandingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.volume = self.root / 'usb'
        self.volume.mkdir()
        self.logo = self.root / 'DJ logo.png'
        self.logo.write_bytes(b'fixture logo bytes')
        self.request = {'volume': str(self.volume), 'artist': 'DJ Example',
                        'website': 'https://vj.tools',
                        'files': [{'role': 'logo', 'path': str(self.logo)}]}

    def test_copy_manifest_and_preserve_previous_bundle_and_music(self):
        music = self.volume / 'music.wav'
        music.write_bytes(b'music preserved')
        result = import_branding(self.request, Job())
        manifest_path = Path(result['manifest'])
        original_manifest = manifest_path.read_bytes()
        manifest = json.loads(original_manifest)
        self.assertEqual((manifest['schema'], manifest['version']), ('vj.tools.dj-branding', 1))
        asset = manifest['files'][0]
        copied = manifest_path.parent / asset['path']
        self.assertEqual(copied.read_bytes(), self.logo.read_bytes())
        self.assertEqual(asset['sha256'], hashlib.sha256(copied.read_bytes()).hexdigest())
        self.assertNotIn(str(self.root), original_manifest.decode())
        second = import_branding(self.request, Job())
        self.assertNotEqual(second['manifest'], str(manifest_path))
        self.assertEqual(manifest_path.read_bytes(), original_manifest)
        self.assertEqual(music.read_bytes(), b'music preserved')
        self.assertEqual(self.logo.read_bytes(), b'fixture logo bytes')

    def test_reject_traversal_before_writing(self):
        self.request['volume'] = str(self.volume / '..' / 'escape')
        with self.assertRaises(ValueError):
            import_branding(self.request, Job())
        self.assertEqual(list(self.volume.iterdir()), [])

    def test_reject_links_and_unsupported_files(self):
        self.request['files'][0]['role'] = 'executable'
        with self.assertRaises(ValueError):
            import_branding(self.request, Job())
        self.request['files'][0]['role'] = 'logo'
        with patch.object(Path, 'is_symlink', return_value=True):
            with self.assertRaises(ValueError):
                import_branding(self.request, Job())

    def test_cancel_leaves_no_published_bundle(self):
        class CancelDuringCopy(Job):
            def progress(self, stage, message):
                raise Cancelled()
        with self.assertRaises(Cancelled):
            import_branding(self.request, CancelDuringCopy())
        self.assertEqual(list((self.volume / 'VJ.Tools/Branding/bundles').iterdir()), [])

    def test_reject_credentials_and_oversized_assets(self):
        self.request['website'] = 'https://user:secret@example.com'
        with self.assertRaises(ValueError):
            import_branding(self.request, Job())
        self.request['website'] = ''
        with patch('builder.branding.MAX_FILE_BYTES', 1):
            with self.assertRaises(ValueError):
                import_branding(self.request, Job())
        self.assertEqual(list(self.volume.iterdir()), [])

    def test_duplicate_bundle_id_never_replaces_files(self):
        with patch('builder.branding.uuid.uuid4', return_value='existing-bundle'):
            first = import_branding(self.request, Job())
            before = Path(first['manifest']).read_bytes()
            with self.assertRaises(FileExistsError):
                import_branding(self.request, Job())
            self.assertEqual(Path(first['manifest']).read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
