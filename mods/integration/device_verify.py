"""Verify support-loader staging on XZ in a private RAM directory, without restarting rbp."""
from pathlib import Path
import argparse
import hashlib
import json
import sys
import uuid

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from device_smoke import command, upload

PIN = "cf381be6d68f64455713524bf94e20d97235e7c93240bb20f7daf0bd8111c0ea"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--host", default="169.254.168.59")
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    receiver = (args.bundle / "lib/libvjtools-xz-receiver.so").read_bytes()
    if hashlib.sha256(receiver).hexdigest() != PIN:
        raise ValueError("Not the pinned receiver")
    loader = (args.bundle / "vjtools-xz-loader.sh").read_bytes()
    expected = (HERE / "loader.sh.in").read_text().replace("@SHA256@", PIN).replace("@MD5@", hashlib.md5(receiver).hexdigest()).encode()
    if loader != expected:
        raise ValueError("Loader differs from the reviewed source")
    remote = "/dev/shm/vj-xs-test-" + uuid.uuid4().hex[:16]
    before = command(args.host, "pidof rbp").strip()
    if not before:
        raise RuntimeError("No running application to check continuity")
    command(args.host, f"mkdir {remote}; mkdir {remote}/lib")
    result = {"rbp_before": before, "receiver_sha256": PIN, "loader_sha256": hashlib.sha256(loader).hexdigest(), "remote": remote}
    try:
        upload(args.host, remote + "/lib/libvjtools-xz-receiver.so", receiver)
        upload(args.host, remote + "/loader.sh", loader)
        text = command(args.host,
            f". {remote}/loader.sh; "
            "LD_PRELOAD=sentinel-not-loaded; export LD_PRELOAD; VJTOOLS_XZ_TESTING=1; export VJTOOLS_XZ_TESTING; "
            f"vjtools_xz_enable {remote} {remote}/stage; echo stage_status=$?; echo first=$LD_PRELOAD; "
            f"vjtools_xz_enable {remote} {remote}/stage; echo repeat_status=$?; echo second=$LD_PRELOAD; unset LD_PRELOAD")
        result["output"] = text
        target = remote + "/stage/" + PIN + "/libvjtools-xz-receiver.so"
        want = "sentinel-not-loaded:" + target
        if "stage_status=0" not in text or "repeat_status=0" not in text or "first=" + want not in text or "second=" + want not in text:
            raise RuntimeError(text)
        result["rbp_after"] = command(args.host, "pidof rbp").strip()
        if result["rbp_after"] != before:
            raise RuntimeError("Application PID changed")
        result["result"] = "PASS actual XZ staging, integrity check and preload preservation; no app restart"
    finally:
        args.evidence.parent.mkdir(parents=True, exist_ok=True)
        args.evidence.write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
        for name in ("lib/libvjtools-xz-receiver.so", "loader.sh", "stage/" + PIN + "/libvjtools-xz-receiver.so"):
            command(args.host, f"rm -f {remote}/{name} {remote}/{name}.b64")
        command(args.host, f"rmdir {remote}/stage/{PIN} {remote}/stage {remote}/lib {remote}")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
