"""Offline inference adapter for approved XZ stem models.

Run under a managed, locked Python environment with ``python -I inference.py``.
This adapter never installs packages, downloads models or contacts a device.
The caller owns the trusted packaged manifest and immutable approved directories.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import importlib.metadata
import json
from pathlib import Path
import sys
import types
import wave

import numpy as np

MANIFEST_PATH = Path(__file__).with_name("models.json")
RATE = 44100
MAX_FRAMES = (128 * 1024 * 1024) // 8  # Two stereo s16 files, four bytes/frame each.


def manifest():
    return json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))


def sha256_file(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


@contextmanager
def verified_artifact(directory, record):
    root = Path(directory).resolve(strict=True)
    name = record["filename"]
    if Path(name).name != name or name in (".", ".."):
        raise ValueError("Artifact filename must be a flat manifest entry")
    candidate = root / name
    if candidate.is_symlink() or candidate.resolve(strict=True).parent != root:
        raise ValueError("Artifact escapes the approved directory")
    with candidate.open("rb") as stream:
        stream.seek(0, 2)
        if stream.tell() != record["bytes"]:
            raise ValueError(f"Artifact size mismatch: {name}")
        stream.seek(0)
        if hashlib.file_digest(stream, "sha256").hexdigest() != record["sha256"]:
            raise ValueError(f"Artifact SHA-256 mismatch: {name}")
        stream.seek(0)
        yield stream


def read_source(path):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate(), source.getcomptype()) != (2, 2, RATE, "NONE"):
            raise ValueError("Input must be uncompressed stereo PCM16 WAV at 44100 Hz")
        frames = source.getnframes()
        if not 0 < frames <= MAX_FRAMES:
            raise ValueError("Input exceeds the native 128 MiB combined stem budget or is empty")
        raw = source.readframes(frames)
        if len(raw) != frames * 4:
            raise ValueError("Input WAV is truncated")
    return np.frombuffer(raw, dtype="<i2").reshape(frames, 2).astype(np.float32) / 32768.0


def checked_audio(audio, frames, label):
    array = np.asarray(audio)
    if array.shape != (frames, 2) or array.dtype.kind not in "fiu":
        raise ValueError(f"{label}: expected exactly {frames} stereo frames")
    if not np.isfinite(array).all():
        raise ValueError(f"{label}: non-finite samples")
    return array


def encode_part(audio):
    peak = float(np.max(np.abs(audio)))
    gain = np.float32(min(1.0, 1.0 / peak)) if peak else np.float32(1.0)
    while float(gain) * peak > 1.0:
        gain = np.nextafter(gain, np.float32(0))
    if not gain > 0 or float(gain) < 1.0 / np.finfo(np.float32).max:
        raise ValueError("Stem gain cannot be represented by the native float mixer")
    stored = np.rint(np.asarray(audio, dtype=np.float64) * float(gain) * 32767.0)
    if not np.isfinite(stored).all() or np.max(np.abs(stored)) > 32767:
        raise ValueError("Stem encoding would clip")
    return stored.astype("<i2"), float(gain)


def postprocess(mix, vocals, drums):
    frames = len(mix)
    if not 0 < frames <= MAX_FRAMES:
        raise ValueError("Invalid frame count or native PCM budget exceeded")
    m = checked_audio(mix, frames, "mix").astype(np.float64)
    v = checked_audio(vocals, frames, "vocals").astype(np.float64)
    d = checked_audio(drums, frames, "drums").astype(np.float64)
    return {"harmonics": encode_part(m - v - d), "vocals": encode_part(v)}


def overlap_inference(mix, infer_chunk, targets, chunk_frames, hop_frames):
    """Bound model memory by chunks; preserve absolute frame positions with OLA."""
    if not 0 < hop_frames <= chunk_frames:
        raise ValueError("Invalid inference chunk/hop")
    count = len(mix)
    result = {target: np.zeros((count, 2), dtype=np.float32) for target in targets}
    weights = np.zeros(count, dtype=np.float32)
    window = np.sin(np.pi * (np.arange(chunk_frames) + 0.5) / chunk_frames) ** 2
    window = window.astype(np.float32)
    for start in range(0, count, hop_frames):
        length = min(chunk_frames, count - start)
        chunk = np.zeros((chunk_frames, 2), dtype=np.float32)
        chunk[:length] = mix[start:start + length]
        estimates = infer_chunk(chunk)
        for target in targets:
            samples = checked_audio(estimates[target], chunk_frames, target)
            result[target][start:start + length] += samples[:length] * window[:length, None]
        weights[start:start + length] += window[:length]
    for target in targets:
        result[target] /= weights[:, None]
        checked_audio(result[target], count, target)
    return result


def runtime_versions(preset, registry):
    expected = dict(registry["runtime_versions"])
    if preset == "vocal-focus":
        expected.update(registry["vocal_focus_dependencies"])
    for name, version in expected.items():
        actual = importlib.metadata.version(name)
        if actual.split("+", 1)[0] != version:
            raise ValueError(f"Managed runtime requires {name}=={version}; found {actual}")
    return dict(sorted((dist.metadata["Name"].lower(), dist.version)
                       for dist in importlib.metadata.distributions() if dist.metadata["Name"]))


def run_umx(mix, model_dir, preset, registry, device):
    import torch
    import openunmix

    targets = preset["umx_targets"]
    separator = openunmix.umxhq(targets=targets, pretrained=False, device=device,
        residual=preset["umx_residual"], niter=preset["niter"],
        wiener_win_len=preset["wiener_win_len"], filterbank="torch")
    for target in targets:
        name = next(name for name in preset["models"] if name.startswith(target + "-"))
        with verified_artifact(model_dir, registry["models"][name]) as stream:
            state = torch.load(stream, map_location="cpu", weights_only=True)
        separator.target_models[target].load_state_dict(state, strict=True)
    separator.eval()

    def infer(chunk):
        tensor = torch.from_numpy(chunk.T.copy()).unsqueeze(0).to(device)
        with torch.inference_mode():
            estimates = separator.to_dict(separator(tensor))
        return {name: estimates[name][0].detach().cpu().numpy().T.copy() for name in targets}

    return overlap_inference(mix, infer, targets, preset["umx_chunk_frames"], preset["umx_hop_frames"])


@contextmanager
def smule_modules(code_dir, registry):
    names = ("flex_attention_utils", "modules", "model", "main")
    if any(name in sys.modules for name in names):
        raise ValueError("Smule module names already loaded; use a fresh isolated adapter process")
    sources = {}
    for name in names:
        record = registry["smule_code"]["files"][name + ".py"]
        with verified_artifact(code_dir, record) as stream:
            sources[name] = stream.read()
    loaded = []
    try:
        for name in names:
            module = types.ModuleType(name)
            module.__file__ = str(Path(code_dir).resolve() / (name + ".py"))
            sys.modules[name] = module
            loaded.append(name)
            exec(compile(sources[name], module.__file__, "exec"), module.__dict__)
        yield sys.modules["main"], sys.modules["model"]
    finally:
        for name in loaded:
            sys.modules.pop(name, None)


def run_smule(mix, model_dir, code_dir, preset, registry, device):
    import torch

    with smule_modules(code_dir, registry) as (api, architecture):
        model = architecture.MelBandRoformerWSA()
        with verified_artifact(model_dir, registry["models"]["mbr-win10-sink8.ckpt"]) as stream:
            state = torch.load(stream, map_location="cpu", weights_only=True)
        model.load_state_dict(state, strict=True)
        model = model.to(device).eval()
        tensor = torch.from_numpy(mix.T.copy())
        output = api.demix(model, tensor, device=device, sample_rate=RATE,
            chunk_size=preset["smule_chunk_seconds"], batch_size=preset["smule_batch_size"])
        return checked_audio(output.detach().cpu().numpy().T, len(mix), "Smule vocals").copy()


def write_output(output_dir, parts, frames, preset_id, registry, versions, source_hash, device):
    if set(parts) != {"harmonics", "vocals"} or not 0 < frames <= MAX_FRAMES:
        raise ValueError("Output must contain exactly two bounded stem parts")
    for name, (pcm, gain) in parts.items():
        if pcm.shape != (frames, 2) or pcm.dtype != np.dtype("<i2") or not np.isfinite(gain) or gain <= 0:
            raise ValueError(f"Invalid encoded part: {name}")
    identity = {"preset": preset_id, "definition": registry["presets"][preset_id],
        "model_hashes": {name: registry["models"][name]["sha256"] for name in registry["presets"][preset_id]["models"]},
        "pipeline_version": registry["pipeline_version"], "runtime_versions": versions,
        "adapter_sha256": sha256_file(__file__), "device": device,
        "smule_code": registry["smule_code"] if preset_id == "vocal-focus" else None}
    digest = hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    result = {"schema_version": 1, "preset": preset_id, "model_id": f"xz-{preset_id}-{digest[:8]}",
        "frames": frames, "sample_rate": RATE, "channels": 2, "source_sha256": source_hash,
        "stems": [], "runtime_versions": versions, "licenses": registry["licenses"],
        "alignment_verified": False, "runtime_validation_verified": False, "identity": identity}
    output = Path(output_dir)
    output.mkdir(exist_ok=False)
    for name, (pcm, gain) in parts.items():
        path = output / (name + ".wav")
        with wave.open(str(path), "wb") as file:
            file.setnchannels(2); file.setsampwidth(2); file.setframerate(RATE)
            file.writeframes(pcm.tobytes(order="C"))
        result["stems"].append({"name": name, "file": path.name, "gain": gain, "sha256": sha256_file(path)})
    (output / "meta").write_text(f"v=1\nframes={frames}\nharmonics={parts['harmonics'][1]:.9g}\nvocals={parts['vocals'][1]:.9g}\n", encoding="ascii")
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True, choices=("umxhq", "vocal-focus"))
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--model-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--smule-code", type=Path)
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    args = parser.parse_args(argv)
    registry = manifest()
    if args.output_dir.exists():
        raise ValueError("Output directory must not already exist")
    if args.preset == "vocal-focus" and args.smule_code is None:
        raise ValueError("Vocal focus requires --smule-code")
    source_hash = sha256_file(args.input)
    mix = read_source(args.input)
    if sha256_file(args.input) != source_hash:
        raise ValueError("Source file changed while reading PCM")
    preset = registry["presets"][args.preset]
    # Validate every required checkpoint before any inference library is imported.
    for name in preset["models"]:
        with verified_artifact(args.model_dir, registry["models"][name]):
            pass
    versions = runtime_versions(args.preset, registry)
    estimates = run_umx(mix, args.model_dir, preset, registry, args.device)
    vocals = estimates["vocals"] if args.preset == "umxhq" else run_smule(
        mix, args.model_dir, args.smule_code, preset, registry, args.device)
    parts = postprocess(mix, vocals, estimates["drums"])
    if sha256_file(args.input) != source_hash:
        raise ValueError("Source file changed during inference")
    result = write_output(args.output_dir, parts, len(mix), args.preset, registry, versions, source_hash, args.device)
    print(json.dumps({"status": "complete", "result": str(args.output_dir / "result.json"), "model_id": result["model_id"]}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, ImportError, RuntimeError, wave.Error) as error:
        print(json.dumps({"status": "failed", "error": str(error)}), file=sys.stderr)
        raise SystemExit(1)
