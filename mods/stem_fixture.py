"""Create a marked synthetic three-part stem fixture without changing existing media."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import shlex
import struct
import subprocess
import wave
from device_smoke import command
from live_io import transfer

RATE = 44100
FRAMES = RATE * 4


def wav(path, samples):
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2); output.setsampwidth(2); output.setframerate(RATE)
        output.writeframes(b"".join(struct.pack("<hh", round(v * 32767), round(v * 32767)) for v in samples))


def cache_key(data, frames):
    payload = struct.pack("<QQ", len(data), frames) + data[:65536]
    if len(data) > 65536:
        payload += data[-65536:]
    h = 1469598103934665603
    for byte in payload:
        h = ((h ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{h:016x}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--volume", default="/media/usb1/sda2")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--bright", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"/media/usb[1-4]/sd[a-z][1-9][0-9]*", args.volume):
        parser.error("Use an inspected mounted USB volume")
    if not 1 <= args.repeat <= 15:
        parser.error("Repeat must be 1..15")
    if args.output.exists():
        parser.error("Choose a new fixture directory")
    args.output.mkdir(parents=True)
    drums, harmonics, vocals = [], [], []
    for i in range(FRAMES):
        t = i / RATE
        beat = t % 0.5
        drum = 0.30 * math.exp(-beat * 28) * math.sin(2 * math.pi * (100 if args.bright else 60) * t)
        if args.bright:
            drum += 0.14 * math.exp(-beat * 140) * math.sin(2 * math.pi * 1800 * t)
            drum += 0.08 * math.exp(-(t % 0.25) * 90) * math.sin(2 * math.pi * 6000 * t)
        drums.append(drum)
        harmonics.append(0.12 * math.sin(2 * math.pi * 220 * t) + 0.08 * math.sin(2 * math.pi * 330 * t))
        vocals.append(0.14 * math.sin(2 * math.pi * (660 if int(t) % 2 == 0 else 880) * t)
                      * (1 if beat < 0.3 else 0))
    source = args.output / "VJTOOLS STEM TEST 120 BPM.wav"
    wav(source, [d + h + v for d, h, v in zip(drums, harmonics, vocals)])
    def repeat_file(path):
        if args.repeat == 1: return
        with wave.open(str(path), "rb") as input:
            data = input.readframes(input.getnframes())
        with wave.open(str(path), "wb") as output:
            output.setnchannels(2); output.setsampwidth(2); output.setframerate(RATE)
            for _ in range(args.repeat): output.writeframesraw(data)
    repeat_file(source)
    for name, samples in (("harmonics", harmonics), ("vocals", vocals)):
        uncompressed = args.output / (name + ".wav")
        wav(uncompressed, samples)
        repeat_file(uncompressed)
        subprocess.run(["ffmpeg", "-v", "error", "-i", str(uncompressed), "-c:a", "flac",
                        str(args.output / (name + ".flac"))], check=True)
    frames = FRAMES * args.repeat
    identity = cache_key(source.read_bytes(), frames)
    source_root = args.volume + "/VJTOOLS_STEM_TEST_20260908" + (f"_{4 * args.repeat}S" if args.repeat > 1 else "")
    if args.bright: source_root += "_BRIGHT"
    cache_root = args.volume + "/mods/stemd-cache/vjtools-fixture-v1/" + identity[:2] + "/" + identity
    result = command("169.254.168.59", f"mkdir {shlex.quote(source_root)}; echo created=$?")
    if "created=0" not in result:
        raise RuntimeError("Could not create a new test folder; existing files left untouched: " + result)
    command("169.254.168.59", f"mkdir -p {shlex.quote(cache_root)}")
    files = {
        source_root + "/" + source.name: source.read_bytes(),
        cache_root + "/harmonics.flac": (args.output / "harmonics.flac").read_bytes(),
        cache_root + "/vocals.flac": (args.output / "vocals.flac").read_bytes(),
        cache_root + "/meta": f"v=1\nframes={frames}\nharmonics=1\nvocals=1\n".encode(),
    }
    for remote, data in files.items():
        transfer("receive", remote, len(data), data)
    receipt = {"source": source_root + "/" + source.name, "cache": cache_root,
               "frames": frames, "rate": RATE, "bpm": 120, "seconds": 4 * args.repeat,
               "parts": {"drums": "kick and bright ticks" if args.bright else "low kick every beat", "harmonics": "steady low chord", "vocals": "higher beeps"},
               "files": {path: hashlib.sha256(data).hexdigest() for path, data in files.items()}}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
