"""Import compatible WAV/FLAC stems into the upstream cache without replacing data.

The source track must remain byte-for-byte identical on the player volume.
Harmonics means the non-drum, non-vocal part; drums are reconstructed by the mod.
No resampling, alignment correction, separation, or normalization is performed.
"""
from __future__ import annotations

import ctypes
import math
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import sys
import subprocess
import tempfile
import wave

MAX_PCM_BYTES = 128 * 1024 * 1024
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
WINDOW = 65536


def _safe_path(value: str | os.PathLike) -> Path:
    path = Path(value).absolute()
    if ".." in path.parts:
        raise ValueError("Parent traversal is not allowed")
    if any(":" in part for part in path.parts[1:]):
        raise ValueError("Alternate data streams are not allowed")
    for component in reversed((path, *path.parents)):
        if component.is_symlink() or (hasattr(component, "is_junction") and component.is_junction()):
            raise ValueError(f"Links and junctions are not allowed: {component}")
    return path


def _regular(value: str | os.PathLike) -> Path:
    path = _safe_path(value)
    if not stat.S_ISREG(path.stat().st_mode):
        raise ValueError(f"Expected a regular file: {path}")
    return path


def inspect_audio(path: str | os.PathLike) -> dict:
    """Inspect PCM16 stereo 44100 Hz WAV, or FLAC with XZ_AUDIO_HELPER.

    XZ_AUDIO_HELPER is a trusted application configuration, never a media field.
    It must point to the bundled native executable, not a shell command.
    """
    path = _regular(path)
    before = path.stat()
    suffix = path.suffix.lower()
    if suffix not in (".wav", ".flac"):
        raise ValueError("Only .wav and .flac audio files are supported")
    helper = os.environ.get("XZ_AUDIO_HELPER")
    if helper:
        if not Path(helper).is_absolute():
            raise ValueError("XZ_AUDIO_HELPER must be an absolute executable path")
        executable = _regular(helper)
        result = subprocess.run([str(executable), "inspect", str(path)],
                                capture_output=True, text=True, encoding="utf-8", timeout=120,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        if result.returncode:
            raise ValueError("Native audio inspection failed: " + result.stderr.strip()[:500])
        try:
            info = json.loads(result.stdout)
            frames = info["frames"]
            valid = (type(frames) is int and frames > 0 and
                     (info["sample_rate"], info["channels"], info["sample_width"]) == (44100, 2, 2)
                     and info["format"] == suffix[1:] and info["pcm_bytes"] == frames * 4)
        except (ValueError, KeyError, TypeError) as exc:
            raise ValueError("Invalid native audio inspection response") from exc
        if not valid or _identity(before) != _identity(path.stat()):
            raise ValueError("Invalid native audio or input changed during inspection")
        return {**info, "path": str(path), "file_bytes": before.st_size}
    if suffix == ".flac":
        raise ValueError("FLAC requires the bundled XZ_AUDIO_HELPER executable")
    try:
        with wave.open(str(path), "rb") as audio:
            frames = audio.getnframes()
            if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getcomptype()) != (2, 2, 44100, "NONE"):
                raise ValueError("Audio must be PCM16 stereo WAV at 44100 Hz")
            if frames <= 0:
                raise ValueError("Audio must contain at least one frame")
            remaining = frames * 4
            while remaining:
                block = audio.readframes(min(16384, remaining // 4))
                if not block or len(block) % 4 or len(block) > remaining:
                    raise ValueError("WAV PCM data is truncated or malformed")
                remaining -= len(block)
    except (wave.Error, EOFError) as exc:
        raise ValueError(f"Invalid compatible WAV: {exc}") from exc
    after = path.stat()
    if _identity(before) != _identity(after):
        raise ValueError("Audio changed during inspection")
    return {"path": str(path), "frames": frames, "sample_rate": 44100,
            "channels": 2, "sample_width": 2, "pcm_bytes": frames * 4,
            "file_bytes": after.st_size}


def _identity(info: os.stat_result) -> tuple:
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns


def track_key(path: str | os.PathLike, frames: int | None = None) -> str:
    """Hash original bytes using upstream's nonstandard FNV offset and windows.

    Explicit frames must be the original track's decoded 44100 Hz frame count.
    Omitting it inspects the source WAV/FLAC using the configured decoder.
    """
    path = _regular(path)
    if frames is None:
        frames = inspect_audio(path)["frames"]
    if type(frames) is not int or not 0 < frames < 2**64:
        raise ValueError("Decoded frame count must be a positive uint64")
    before = path.stat()
    length = before.st_size
    if length <= 0:
        raise ValueError("Source track is empty")
    with path.open("rb") as source:
        chunks = [struct.pack("<QQ", length, frames), source.read(min(length, WINDOW))]
        if length > WINDOW:
            source.seek(length - WINDOW)
            chunks.append(source.read(WINDOW))
    if _identity(before) != _identity(path.stat()):
        raise ValueError("Source changed during hashing")
    value = FNV_OFFSET
    for block in chunks:
        for byte in block:
            value = ((value ^ byte) * FNV_PRIME) & 0xffffffffffffffff
    return f"{value:016x}"


def _gain(value: float) -> float:
    try:
        value = struct.unpack("<f", struct.pack("<f", float(value)))[0]
        reciprocal = struct.unpack("<f", struct.pack("<f", 1.0 / value))[0]
    except (OverflowError, ZeroDivisionError, TypeError, ValueError) as exc:
        raise ValueError("Normalization gain must be finite, positive and usable as float32") from exc
    # The native metadata parser rejects strtof underflow, including subnormals.
    if value < 2**-126 or not math.isfinite(value) or not math.isfinite(reciprocal):
        raise ValueError("Normalization gain must be finite, positive and usable as float32")
    return value


def _publish_new(source: Path, target: Path) -> None:
    # POSIX rename can replace an empty directory, so require no-replace semantics.
    if sys.platform == "win32":
        os.rename(source, target)
    elif sys.platform.startswith("linux"):
        libc = ctypes.CDLL(None, use_errno=True)
        rename = getattr(libc, "renameat2", None)
        if rename is None:
            raise OSError("Atomic no-replace directory publication is unavailable")
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        if rename(-100, os.fsencode(source), -100, os.fsencode(target), 1):
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error), str(target))
    else:
        raise OSError("Atomic no-replace cache publication supports Windows and Linux")


def validate_model_id(separation_id):
    if (not isinstance(separation_id,str) or not re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]{0,95}",separation_id)
            or separation_id.endswith('.') or separation_id.split('.')[0].upper() in
            {'CON','PRN','AUX','NUL',*(f'COM{i}' for i in range(1,10)),*(f'LPT{i}' for i in range(1,10))}):
        raise ValueError('Separation ID must be a safe portable directory name')
    return separation_id

def import_cache(volume: str | os.PathLike, source: str | os.PathLike,
                 harmonics: str | os.PathLike, vocals: str | os.PathLike,
                 separation_id: str, harmonics_gain: float = 1.0,
                 vocals_gain: float = 1.0) -> dict:
    """Add one complete upstream cache entry. Existing entries are never replaced."""
    validate_model_id(separation_id)
    gains = [_gain(harmonics_gain), _gain(vocals_gain)]
    volume = _safe_path(volume)
    if not volume.is_dir():
        raise ValueError("Volume must be an existing directory")
    paths = [_regular(p) for p in (source, harmonics, vocals)]
    snapshots = [_identity(p.stat()) for p in paths]
    audio = [inspect_audio(p) for p in paths]
    frames = audio[0]["frames"]
    if any(item["frames"] != frames for item in audio):
        raise ValueError("Source and both stems must have exactly matching frame counts")
    if sum(item["pcm_bytes"] for item in audio[1:]) > MAX_PCM_BYTES:
        raise ValueError("Combined stem PCM exceeds the 128 MiB device limit")
    key = track_key(paths[0], frames)
    parent = volume / "mods" / "stemd-cache" / separation_id / key[:2]
    _safe_path(parent)
    parent.mkdir(parents=True, exist_ok=True)
    target = parent / key
    _safe_path(target)
    if target.exists():
        raise FileExistsError(f"Cache entry already exists: {target}")
    temporary = Path(tempfile.mkdtemp(prefix=".import-", dir=parent))
    try:
        for path, part in zip(paths[1:], ("harmonics", "vocals")):
            name = part + path.suffix.lower()
            shutil.copyfile(path, temporary / name)
            inspect_audio(temporary / name)
        (temporary / "meta").write_text(
            f"v=1\nframes={frames}\nharmonics={gains[0]:.9g}\nvocals={gains[1]:.9g}\n", encoding="ascii")
        if snapshots != [_identity(p.stat()) for p in paths]:
            raise ValueError("Input audio changed during import")
        _safe_path(parent)
        _safe_path(target)
        _publish_new(temporary, target)
    finally:
        if temporary.exists():
            _safe_path(temporary)
            shutil.rmtree(temporary)
    select_model(volume,key,separation_id)
    return {"key": key, "directory": str(target), "separation_id": separation_id,
            "frames": frames, "sample_rate": 44100, "harmonics_gain": gains[0],
            "vocals_gain": gains[1], "pcm_bytes": frames * 8}

def select_model(volume,key,separation_id):
    validate_model_id(separation_id)
    if not re.fullmatch('[0-9a-f]{16}',key):raise ValueError('Invalid track key')
    volume=_safe_path(volume)
    target=_safe_path(volume/'mods/stemd-cache'/separation_id/key[:2]/key)
    if not target.is_dir():raise ValueError('Selected cache is missing')
    choice_parent = _safe_path(volume / "mods" / "xz-mods" / "cache-choice")
    choice_parent.mkdir(parents=True, exist_ok=True)
    choice = _safe_path(choice_parent / (key + ".txt"))
    handle, pending = tempfile.mkstemp(prefix=".choice-", dir=choice_parent)
    pending = Path(pending)
    try:
        with os.fdopen(handle, "w", encoding="ascii", newline="\n") as output:
            output.write(separation_id + "\n")
            output.flush()
            os.fsync(output.fileno())
        _safe_path(choice)
        os.replace(pending, choice)
    finally:
        if pending.exists():
            _safe_path(pending)
            pending.unlink()
