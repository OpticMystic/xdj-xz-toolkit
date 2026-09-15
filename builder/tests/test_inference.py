"""Offline tests: stdlib + NumPy only, no torch/model/network/device execution."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import tempfile
import unittest
from unittest import mock
import wave

import numpy as np

ADAPTER = Path(__file__).resolve().parents[1] / "inference.py"
spec = importlib.util.spec_from_file_location("xz_builder_inference", ADAPTER)
inference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inference)


def write_wav(path, samples, rate=44100, channels=2):
    with wave.open(str(path), "wb") as target:
        target.setnchannels(channels)
        target.setsampwidth(2)
        target.setframerate(rate)
        target.writeframes(np.asarray(samples, dtype="<i2").tobytes())


class ManifestTests(unittest.TestCase):
    def test_all_smule_source_verified_before_any_execution(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = b"raise RuntimeError('source executed before integrity check')\n"
            records = {}
            for name in ("flex_attention_utils.py", "modules.py", "model.py", "main.py"):
                (root / name).write_bytes(source)
                records[name] = {"filename": name, "bytes": len(source),
                                 "sha256": hashlib.sha256(source).hexdigest()}
            records["main.py"]["sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                with inference.smule_modules(root, {"smule_code": {"files": records}}):
                    self.fail("Unverified source executed")

    def test_only_approved_presets_and_original_licenses(self):
        registry = inference.manifest()
        self.assertEqual(set(registry["presets"]), {"umxhq", "vocal-focus"})
        self.assertEqual(registry["sample_rate"], 44100)
        self.assertEqual(registry["channels"], 2)
        self.assertEqual(registry["max_combined_pcm_bytes"], inference.MAX_FRAMES * 8)
        self.assertEqual(registry["presets"]["umxhq"]["umx_targets"], ["vocals", "drums", "bass", "other"])
        self.assertTrue(registry["presets"]["vocal-focus"]["umx_residual"])
        for preset in registry["presets"].values():
            self.assertEqual(preset["harmonics"], "mix-vocals-drums")
            for name in preset["models"]:
                self.assertNotIn("demucs", name.lower())
                self.assertNotIn("umxl", name.lower())
                record = registry["models"][name]
                self.assertEqual(record["filename"], name)
                self.assertRegex(record["sha256"], r"^[0-9a-f]{64}$")
                self.assertTrue(record["url"].startswith("https://"))
                license_record = registry["licenses"][record["license"]]
                self.assertEqual(license_record["spdx"], "MIT")
                self.assertIn("Permission is hereby granted", license_record["text"])
        self.assertEqual(registry["models"]["drums-9619578f.pth"]["sha256"],
            "9619578f885c54737cb0234f9f9a4a679ee4f31438fd77fd1dbe02bb16c2da0a")
        self.assertEqual(set(registry["smule_code"]["files"]),
            {"main.py", "model.py", "modules.py", "flex_attention_utils.py"})

    def test_verify_before_use_and_reject_path_escape(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "model.pth").write_bytes(b"trusted bytes")
            record = {"filename": "model.pth", "bytes": 13,
                      "sha256": hashlib.sha256(b"trusted bytes").hexdigest()}
            with inference.verified_artifact(root, record) as handle:
                self.assertEqual(handle.read(), b"trusted bytes")
            (root / "model.pth").write_bytes(b"changed bytes")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                with inference.verified_artifact(root, record):
                    self.fail("Corrupt checkpoint reached caller")
            record["filename"] = "../outside.pth"
            with self.assertRaisesRegex(ValueError, "flat"):
                with inference.verified_artifact(root, record):
                    self.fail("Escaping checkpoint reached caller")


class InputTests(unittest.TestCase):
    def test_exact_pcm_shape_scaling_and_source_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "source.wav"
            data = np.array([[32767, -32768], [123, -456]], dtype="<i2")
            write_wav(path, data)
            original = path.read_bytes()
            output = inference.read_source(path)
            np.testing.assert_array_equal(output, data.astype(np.float32) / 32768)
            self.assertEqual(path.read_bytes(), original)
            write_wav(path, data, rate=48000)
            with self.assertRaisesRegex(ValueError, "44100"):
                inference.read_source(path)

    def test_cap_checked_from_header_before_large_read(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "huge.wav"
            data_bytes = (inference.MAX_FRAMES + 1) * 4
            path.write_bytes(struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", data_bytes + 36,
                b"WAVE", b"fmt ", 16, 1, 2, 44100, 176400, 4, 16, b"data", data_bytes))
            with self.assertRaisesRegex(ValueError, "128 MiB"):
                inference.read_source(path)

    def test_missing_artifact_rejected_before_runtime(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source.wav"
            write_wav(source, np.zeros((10, 2), dtype="<i2"))
            with mock.patch.object(inference, "runtime_versions", side_effect=AssertionError("runtime imported")):
                with self.assertRaises(FileNotFoundError):
                    inference.main(["--preset", "umxhq", "--input", str(source),
                        "--model-dir", str(root), "--output-dir", str(root / "output")])
            self.assertFalse((root / "output").exists())


class PostprocessTests(unittest.TestCase):
    def test_residual_mapping_and_per_stem_gain_restore(self):
        mix = np.array([[0.5, -0.25], [0.1, -0.1]])
        vocals = np.array([[2.0, -2.0], [0.5, -0.5]])
        drums = np.array([[2.0, -1.0], [0.2, -0.2]])
        parts = inference.postprocess(mix, vocals, drums)
        self.assertEqual(set(parts), {"harmonics", "vocals"})
        h_pcm, h_gain = parts["harmonics"]
        v_pcm, v_gain = parts["vocals"]
        self.assertLess(h_gain, v_gain)
        self.assertLess(v_gain, 1)
        h = h_pcm.astype(np.float64) / 32767 / h_gain
        v = v_pcm.astype(np.float64) / 32767 / v_gain
        np.testing.assert_allclose(h, mix - vocals - drums, atol=1e-4)
        np.testing.assert_allclose(v, vocals, atol=1e-4)
        np.testing.assert_allclose(mix - h - v, drums, atol=1e-4)
        for pcm, gain in parts.values():
            self.assertLessEqual(int(np.abs(pcm.astype(np.int32)).max()), 32767)
            self.assertEqual(np.float32(f"{gain:.9g}"), np.float32(gain))

    def test_invalid_shapes_nonfinite_and_budget(self):
        mix = np.zeros((3, 2))
        with self.assertRaisesRegex(ValueError, "exactly"):
            inference.postprocess(mix, np.zeros((2, 2)), mix)
        broken = mix.copy(); broken[0, 0] = np.nan
        with self.assertRaisesRegex(ValueError, "non-finite"):
            inference.postprocess(mix, broken, mix)
        with mock.patch.object(inference, "MAX_FRAMES", 2):
            with self.assertRaisesRegex(ValueError, "budget"):
                inference.postprocess(mix, mix, mix)
        parts = inference.postprocess(mix, mix, mix)
        self.assertEqual(parts["vocals"][1], 1)

    def test_overlap_keeps_edges_length_channels_and_input(self):
        mix = np.arange(74, dtype=np.float32).reshape(37, 2) / 100
        original = mix.copy()
        calls = []
        def model(chunk):
            calls.append(chunk.shape)
            return {"vocals": chunk * 0.25, "drums": chunk * 0.5}
        output = inference.overlap_inference(mix, model, ["vocals", "drums"], 16, 8)
        np.testing.assert_allclose(output["vocals"], mix * 0.25, atol=1e-7)
        np.testing.assert_allclose(output["drums"], mix * 0.5, atol=1e-7)
        np.testing.assert_array_equal(mix, original)
        self.assertEqual(calls, [(16, 2)] * 5)

    def test_new_directory_cache_meta_and_identity(self):
        registry = inference.manifest()
        mix = np.zeros((3, 2))
        parts = inference.postprocess(mix, mix, mix)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            a = inference.write_output(root / "a", parts, 3, "umxhq", registry, {"numpy": "test"}, "a"*64, "cpu")
            b = inference.write_output(root / "b", parts, 3, "umxhq", registry, {"numpy": "test"}, "b"*64, "cpu")
            self.assertEqual(a["model_id"], b["model_id"])
            self.assertTrue(re.fullmatch(r"[A-Za-z0-9._-]{1,31}", a["model_id"]))
            self.assertFalse(a["alignment_verified"])
            self.assertEqual((root / "a/meta").read_text(), "v=1\nframes=3\nharmonics=1\nvocals=1\n")
            self.assertEqual({s["name"] for s in a["stems"]}, {"harmonics", "vocals"})
            self.assertIn("smule-mit", json.loads((root / "a/result.json").read_text())["licenses"])
            for name in ("harmonics", "vocals"):
                with wave.open(str(root / f"a/{name}.wav"), "rb") as wav:
                    self.assertEqual((wav.getnframes(), wav.getnchannels(), wav.getframerate()), (3, 2, 44100))
            with self.assertRaises(FileExistsError):
                inference.write_output(root / "a", parts, 3, "umxhq", registry, {}, "x", "cpu")
            changed = copy.deepcopy(registry); changed["pipeline_version"] += "-new"
            c = inference.write_output(root / "c", parts, 3, "umxhq", changed, {"numpy": "test"}, "a"*64, "cpu")
            self.assertNotEqual(a["model_id"], c["model_id"])


if __name__ == "__main__":
    unittest.main()
