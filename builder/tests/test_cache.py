import ctypes
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import wave

MODULE = Path(__file__).resolve().parents[1] / "cache.py"
spec = importlib.util.spec_from_file_location("xz_builder_cache", MODULE)
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)


def fixture(path, frames=20000, rate=44100, channels=2, width=2):
    with wave.open(str(path), "wb") as out:
        out.setparams((channels, width, rate, frames, "NONE", "NONE"))
        out.writeframes(bytes((i % 251 for i in range(frames * channels * width))))
    return path


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.volume = self.root / "volume"
        self.volume.mkdir()
        self.source = fixture(self.root / "source.wav")
        self.harmonics = fixture(self.root / "h.wav")
        self.vocals = fixture(self.root / "v.wav")

    def run_import(self, **kwargs):
        return cache.import_cache(self.volume, self.source, self.harmonics,
                                  self.vocals, kwargs.pop("separation_id", "model-v1"), **kwargs)

    def test_real_wav_import_and_no_clobber(self):
        original = self.source.read_bytes()
        receipt = self.run_import(harmonics_gain=0.25, vocals_gain=2)
        directory = Path(receipt["directory"])
        self.assertEqual(directory.relative_to(self.volume).parts,
                         ("mods", "stemd-cache", "model-v1", receipt["key"][:2], receipt["key"]))
        self.assertEqual((directory / "meta").read_text(),
                         "v=1\nframes=20000\nharmonics=0.25\nvocals=2\n")
        self.assertEqual((directory / "harmonics.wav").read_bytes(), self.harmonics.read_bytes())
        with self.assertRaises(FileExistsError):
            self.run_import()
        self.assertEqual(self.source.read_bytes(), original)
        self.assertEqual(sorted(p.name for p in directory.iterdir()), ["harmonics.wav", "meta", "vocals.wav"])

    def test_hash_matches_independent_integer_oracle_for_both_windows(self):
        for frames in (100, 20000):
            fixture(self.source, frames)
            data = self.source.read_bytes()
            payload = struct.pack("<QQ", len(data), frames) + data[:65536]
            if len(data) > 65536:
                payload += data[-65536:]
            value = 1469598103934665603
            for byte in payload:
                value = ((value ^ byte) * 1099511628211) % (1 << 64)
            self.assertEqual(cache.track_key(self.source), f"{value:016x}")

    def test_incompatible_and_truncated_audio(self):
        for changes in ({"rate": 48000}, {"channels": 1}, {"width": 3}, {"frames": 0}):
            fixture(self.vocals, **changes)
            with self.assertRaises(ValueError):
                self.run_import()
        fixture(self.vocals)
        self.vocals.write_bytes(self.vocals.read_bytes()[:-4])
        with self.assertRaises(ValueError):
            self.run_import()
        self.assertFalse((self.volume / "mods").exists())

    def test_frames_budget_and_gains(self):
        fixture(self.vocals, 19999)
        with self.assertRaises(ValueError):
            self.run_import()
        fixture(self.vocals)
        with patch.object(cache, "MAX_PCM_BYTES", 100):
            with self.assertRaises(ValueError):
                self.run_import()
        for gain in (0, -1, float("nan"), float("inf"), 1e-50, 1e-38, 1e40):
            with self.assertRaises(ValueError):
                self.run_import(vocals_gain=gain)

    def test_unsafe_names_and_links(self):
        for name in ("..", "../evil", "x/y", "x\\y", "x:", "NUL", "COM1.wav", "foo."):
            with self.assertRaises(ValueError):
                self.run_import(separation_id=name)
        with self.assertRaises(ValueError):
            cache.inspect_audio(self.root / "sub" / ".." / "source.wav")
        link = self.volume / "mods"
        try:
            link.symlink_to(self.root, target_is_directory=True)
        except OSError:
            self.skipTest("Creating symlinks is unavailable")
        with self.assertRaises(ValueError):
            self.run_import()

    def test_failed_publication_cleans_only_own_temp(self):
        key = cache.track_key(self.source)
        parent = self.volume / "mods" / "stemd-cache" / "model-v1" / key[:2]
        parent.mkdir(parents=True)
        sentinel = parent / ".import-someone-else"
        sentinel.mkdir()
        with patch.object(cache, "_publish_new", side_effect=OSError("test failure")):
            with self.assertRaises(OSError):
                self.run_import()
        self.assertEqual(list(parent.iterdir()), [sentinel])

    def test_atomic_publication_refuses_even_empty_existing_directory(self):
        source, target = self.root / "new", self.root / "existing"
        source.mkdir()
        target.mkdir()
        with self.assertRaises(OSError):
            cache._publish_new(source, target)
        self.assertTrue(source.is_dir())
        self.assertTrue(target.is_dir())

    @unittest.skipUnless(os.environ.get("XZ_CACHE_ZIG"), "Set XZ_CACHE_ZIG for native C interoperability")
    def test_existing_c_lookup_reads_generated_cache(self):
        audio = Path(os.environ.get("XZ_CACHE_AUDIO", str(MODULE.parents[1] / "mods" / "audio")))
        library = self.root / ("lookup.dll" if os.name == "nt" else "lookup.so")
        runtime = (audio / "runtime.c").read_text()
        discovery = runtime.split("static int discover_cache(", 1)[1].split("\nstruct cancel_context", 1)[0]
        wrapper = self.root / "discovery.c"
        wrapper.write_text('#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <dirent.h>\n'
            '#include "runtime.h"\n#include "native_reader.h"\nstatic int discover_cache(' + discovery +
            '\nint builder_test_discover(const char *path, int64_t frames, struct xz_stem_entry *out) {'
            'struct xz126_deck_source source = {0}; snprintf(source.path,sizeof(source.path),"%s",path);'
            'source.file_frames=frames;return discover_cache(&source,out);}\n'
            'void builder_test_selection(const char *id) {\n#ifdef _WIN32\n'
            '_putenv_s("XZ_STEM_SEPARATION_ID",id);\n#else\nsetenv("XZ_STEM_SEPARATION_ID",id,1);\n#endif\n}\n')
        subprocess.run([os.environ["XZ_CACHE_ZIG"], "cc", "-shared", "-O2", "-UNDEBUG",
                        str(audio / "stem_cache.c"), str(wrapper), "-I", str(audio), "-o", str(library)], check=True,
                       capture_output=True)
        native = ctypes.CDLL(str(library))
        if os.name == "nt":
            import _ctypes
            self.addCleanup(_ctypes.FreeLibrary, native._handle)
        class Entry(ctypes.Structure):
            _fields_ = [("harmonics", ctypes.c_char * 4096), ("vocals", ctypes.c_char * 4096),
                        ("frames", ctypes.c_int64), ("hgain", ctypes.c_float), ("vgain", ctypes.c_float)]
        native.xz_stem_cache_lookup.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
                                               ctypes.c_int64, ctypes.POINTER(Entry)]
        native.xz_stem_cache_lookup.restype = ctypes.c_int
        native.builder_test_discover.argtypes = [ctypes.c_char_p, ctypes.c_int64, ctypes.POINTER(Entry)]
        native.builder_test_discover.restype = ctypes.c_int
        native.builder_test_selection.argtypes = [ctypes.c_char_p]
        native.builder_test_selection(b"")
        receipt = self.run_import(harmonics_gain=0.25, vocals_gain=2)
        result = Entry()
        playing = fixture(self.volume / "playing.wav")
        native_path = playing.as_posix()
        if os.name == "nt":
            native_path = native_path[len(playing.drive):]
        def discover():
            return native.builder_test_discover(os.fsencode(native_path), 20000, ctypes.byref(result))
        self.assertEqual(native.xz_stem_cache_lookup(os.fsencode(self.volume), b"model-v1",
                                                    os.fsencode(self.source), 20000,
                                                    ctypes.byref(result)), 0)
        self.assertEqual((result.frames, result.hgain, result.vgain), (20000, 0.25, 2))
        self.assertEqual(Path(os.fsdecode(result.harmonics)), Path(receipt["directory"]) / "harmonics.wav")
        second = self.run_import(separation_id="other-model", harmonics_gain=0.5)
        preference = self.volume / "mods/xz-mods/cache-choice" / (receipt["key"] + ".txt")
        self.assertEqual(preference.read_text(), "other-model\n")
        def lookup(selected=None):
            return native.xz_stem_cache_lookup(os.fsencode(self.volume), selected,
                os.fsencode(self.source), 20000, ctypes.byref(result))
        self.assertEqual(lookup(), 0)
        self.assertEqual(result.hgain, 0.5)
        self.assertEqual(discover(), 9)
        self.assertEqual(result.hgain, 0.5)
        self.assertEqual(lookup(b"model-v1"), 0)
        self.assertEqual(result.hgain, 0.25)
        self.assertTrue(Path(receipt["directory"]).is_dir())
        self.assertTrue(Path(second["directory"]).is_dir())
        for malformed in (b"../escape\n", b"..\n", b"other-model\x00ignored", b"other-model\nextra", b"x"*100):
            preference.write_bytes(malformed)
            self.assertEqual(lookup(), -1)
            self.assertEqual(discover(), 12)
            self.assertEqual(lookup(b"model-v1"), 0)
            native.builder_test_selection(b"model-v1")
            self.assertEqual(discover(), 9)
            self.assertEqual(result.hgain, 0.25)
            native.builder_test_selection(b"")
        preference.write_text("missing-model\n")
        self.assertEqual(lookup(), 1)
        self.assertEqual(discover(), 12)
        preference.unlink()
        self.assertEqual(lookup(), 1)
        self.assertEqual(discover(), 5)
        outside = self.root / "outside-preference.txt"
        outside.write_text("other-model\n")
        try:
            preference.symlink_to(outside)
        except OSError:
            return
        self.assertEqual(lookup(), -1)


if __name__ == "__main__":
    unittest.main()
