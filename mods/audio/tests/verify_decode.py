"""POSIX host test: compile C, generate real WAV/FLAC with ffmpeg, decode both."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import wave

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="xz-real-decode-") as temporary:
    root = Path(temporary)
    wav = root / "vocals.wav"
    flac = root / "harmonics.flac"
    with wave.open(str(wav), "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(44100)
        f.writeframes(b"".join(struct.pack("<h", i % 60001 - 30000) for i in range(88200)))
    subprocess.run(["ffmpeg", "-v", "error", "-i", str(wav), "-c:a", "flac", str(flac)], check=True)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                    "-I", str(HERE.parent), str(HERE / "test_decode.c"),
                    str(HERE.parent / "stem_decode.c"), "-lm", "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test"), str(flac), str(wav)], check=True)
    truncated = root / "truncated.flac"
    truncated.write_bytes(flac.read_bytes()[:256])
    subprocess.run([str(root / "test"), str(truncated), str(wav), "invalid"], check=True)
    mono = root / "mono.wav"
    with wave.open(str(mono), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(44100)
        f.writeframes(b"\0\0" * 44100)
    subprocess.run([str(root / "test"), str(flac), str(mono), "invalid"], check=True)
    subprocess.run([str(root / "test"), str(root / "missing.flac"), str(wav), "invalid"], check=True)
    print("truncated FLAC, mono WAV and missing file rejected without published PCM")
