"""Build a private XZ 1.26 development boot image. Does not write USB or deploy."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent
VENDOR = ROOT.parent / "vendor"
sys.path.insert(0, str(VENDOR))
from tools.xz_firmware.image_builder import build_autoexec_bin, extract_autoexec_file, verify_autoexec_bin
from tools.xz_firmware.payload import write_runtime_payload
from tools.xz_patcher.deck_select_4deck import patch_rbp as deck_patch
from tools.xz_patcher.waveform_3band import patch_rbp as waveform_patch
from build import inspect


def sha(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--mode", choices=("observer", "experimental"), default="observer")
    parser.add_argument("--gui-pack", type=pathlib.Path, help="Locally generated GUI pack, bound from RAM by the loader")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output exists; use a new private development directory")
    runtime = args.build / "libxz-mods-development.so"
    inspect(runtime)
    evidence = json.loads((args.build / "device-smoke.json").read_text(encoding="utf8"))
    if not evidence.get("result", "").startswith("PASS") or evidence["artifact_sha256"].get(runtime.name) != sha(runtime):
        parser.error("An isolated on-device load check for this exact runtime is required")
    if args.mode == "experimental":
        receiver = args.build / "libxz-directfb-mods-test.so"
        inspect(receiver)
        if evidence["artifact_sha256"].get(receiver.name) != sha(receiver):
            parser.error("The paired display bridge has not passed the isolated device check")
    else:
        receiver = VENDOR / "build/libxz-directfb-hook-v49-exclusive-abi14.so"
        if sha(receiver) != "cf381be6d68f64455713524bf94e20d97235e7c93240bb20f7daf0bd8111c0ea":
            parser.error("The observer requires the unchanged verified VJ receiver")
    stock = VENDOR / "decrypted_iso/pdj/extracted/pdj/rbp"
    if sha(stock) != "6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2":
        parser.error("Unknown firmware input; expected the inspected XZ 1.26 application")
    rbp = deck_patch(waveform_patch(stock.read_bytes()))
    if hashlib.md5(rbp).hexdigest() != "6a7ccb454e52afa26a73f3380706c9ca":
        parser.error("Patched application differs from the currently verified XZ build")
    args.output.mkdir(parents=True)
    destination = args.output / "autoexec.bin"
    pending = args.output / "autoexec.bin.pending"
    key = VENDOR / "keys/aes256.key"
    with tempfile.TemporaryDirectory(prefix="xz-private-mods-") as temporary:
        staging = pathlib.Path(temporary)
        payload = write_runtime_payload(staging, rbp,
                    features=("waveform_color_mode3", "deck_select_gate_not_four_native_players", "vjtools_receiver", "development_mods"),
                    directfb_hook_path=receiver, mods_runtime_path=runtime, mods_mode=args.mode,
                    gui_pack_path=args.gui_pack)
        notices = staging / "licenses"
        notices.mkdir(exist_ok=True)
        (notices / "Barlow-OFL.txt").write_bytes((ROOT / "ui/fonts/OFL.txt").read_bytes())
        build_autoexec_bin(staging, pending, key)
        structure = verify_autoexec_bin(pending, key)
        roundtrip_files = ["autoexec.sh", "rbp.patched", "mods-mode", "tools/libxz-mods.so", "tools/libxz-directfb-hook.so", "licenses/Barlow-OFL.txt"]
        if args.gui_pack:
            roundtrip_files += ["gui/imagedata.dat", "gui/imagedata.dat.md5"]
        for relative in roundtrip_files:
            actual = extract_autoexec_file(pending, key, "/" + relative)
            if actual != (staging / relative).read_bytes():
                raise ValueError(f"Encrypted payload round-trip failed: {relative}")
    pending.rename(destination)
    report = {"firmware": "XDJ-XZ 1.26", "profile": args.mode, "development_only": True,
              "contains_firmware_application": True, "redistributable_support_bundle": False,
              "hardware_features_verified": False, "image_sha256": sha(destination),
              "runtime_sha256": sha(runtime), "receiver_sha256": sha(receiver),
              "payload": payload, "iso": structure}
    (args.output / "manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    (args.output / "README.txt").write_text(
        "PRIVATE DEVELOPMENT IMAGE - XDJ-XZ 1.26 ONLY\n"
        f"Profile: {args.mode}\n"
        "Built and round-trip verified locally. Not copied to USB or loaded.\n"
        "Use a fresh boot with the existing XZ RAM loader procedure.\n"
        "Observer profile preserves stock cue dispatch and logs pad/play input.\n"
        "Experimental profile exposes incomplete DJ adapters and the MODS panel.\n"
        "This is not a full port or a public firmware release. It contains the firmware application.\n"
        "The separate integration/ support bundle contains no firmware or keys.\n", encoding="utf8")
    print(f"Private {args.mode} image: {destination}; SHA256 {report['image_sha256']}")


if __name__ == "__main__":
    main()
