"""Command-line builder and hardware-proof auditor for XDJ-XZ firmware 1.26."""

import argparse
import hashlib
import pathlib
import sys
import tempfile

ROOT_DIR = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from tools.xz_firmware.image_builder import build_autoexec_bin, extract_autoexec_file, verify_autoexec_bin
from tools.xz_firmware.key_manager import get_key_path
from tools.xz_firmware.payload import write_runtime_payload
from tools.xz_patcher.deck_select_4deck import PATCHES_1_26 as DECK_PATCHES
from tools.xz_patcher.deck_select_4deck import patch_rbp as patch_4deck
from tools.xz_patcher.ui_watermark import PATCHES_1_26 as SETTINGS_MARKER_PATCHES
from tools.xz_patcher.waveform_3band import PATCHES_1_26 as WAVEFORM_PATCHES
from tools.xz_patcher.waveform_3band import patch_rbp as patch_3band


def cmd_build_usb(args):
    target_drive = pathlib.Path(args.target_drive)
    if not target_drive.is_dir():
        raise SystemExit(f"Error: Target directory {target_drive} does not exist.")

    key_path = get_key_path(args.key)
    stock_rbp = pathlib.Path(args.stock_rbp) if args.stock_rbp else ROOT_DIR / "decrypted_iso/pdj/extracted/pdj/rbp"
    if not stock_rbp.is_file():
        raise SystemExit(f"Error: Stock rbp binary not found at {stock_rbp}")

    rbp_data = stock_rbp.read_bytes()
    features = []
    print(f"Loaded stock rbp binary ({len(rbp_data):,} bytes)")
    print("\nApplying firmware 1.26 patches:")

    if not args.no_3band:
        rbp_data = patch_3band(rbp_data)
        features.append("waveform_color_mode3")
        print("  [+] Native mode 3 PWV4/PWV5 color waveform path")

    if not args.no_4deck:
        rbp_data = patch_4deck(rbp_data)
        features.append("deck_select_4deck_experimental")
        print("  [+] Experimental 4-Deck handler gate (activation path unverified)")

    gui_pack_path = pathlib.Path(args.gui_pack) if args.gui_pack else None
    fb_overlay_helper_path = pathlib.Path(args.fb_overlay_helper) if args.fb_overlay_helper else None
    directfb_hook_path = pathlib.Path(args.directfb_hook) if args.directfb_hook else None
    gui_ip_patch_path = pathlib.Path(args.gui_ip_patch) if args.gui_ip_patch else None
    if gui_pack_path:
        if not gui_pack_path.is_file():
            raise SystemExit(f"Error: GUI pack not found at {gui_pack_path}")
        features.append("ram_gui_theme")
    if fb_overlay_helper_path:
        if not fb_overlay_helper_path.is_file():
            raise SystemExit(f"Error: framebuffer helper not found at {fb_overlay_helper_path}")
        features.append("fb_overlay_helper_staged")
    if directfb_hook_path:
        if not directfb_hook_path.is_file():
            raise SystemExit(f"Error: DirectFB hook not found at {directfb_hook_path}")
        features.append("vjtools_xdj_filmstrip_receiver")
    if gui_ip_patch_path:
        if not gui_ip_patch_path.is_file():
            raise SystemExit(f"Error: GUI IP patcher not found at {gui_ip_patch_path}")
        features.append("runtime_ip_logo")

    with tempfile.TemporaryDirectory() as td:
        staging = pathlib.Path(td)
        payload_meta = write_runtime_payload(
            staging,
            rbp_data,
            features=features,
            enable_telnet=args.telnet,
            enable_vj_bridge_marker=args.vj_bridge_marker,
            gui_pack_path=gui_pack_path,
            fb_overlay_helper_path=fb_overlay_helper_path,
            directfb_hook_path=directfb_hook_path,
            gui_ip_patch_path=gui_ip_patch_path,
        )
        if args.telnet:
            print("  [+] Diagnostic Telnet shell (TCP 2323)")
        if args.vj_bridge_marker:
            print("  [i] VJ bridge marker only; an on-device bridge daemon is not implemented")
        print(f"  [+] Runtime rbp MD5: {payload_meta['rbp_md5']}")
        if payload_meta["gui_md5"]:
            print(f"  [+] RAM GUI pack MD5: {payload_meta['gui_md5']}")
        if payload_meta["fb_overlay_helper_md5"]:
            print(f"  [+] Staged fb overlay helper MD5: {payload_meta['fb_overlay_helper_md5']}")
        if payload_meta["directfb_hook_md5"]:
            print(f"  [+] RAM-only VJ.Tools filmstrip receiver MD5: {payload_meta['directfb_hook_md5']}")
        if payload_meta["gui_ip_patch_md5"]:
            print(f"  [+] Runtime GUI IP patcher MD5: {payload_meta['gui_ip_patch_md5']}")

        out_bin = target_drive / "autoexec.bin"
        print("\nAuthoring encrypted autoexec.bin...")
        size = build_autoexec_bin(staging, out_bin, key_path)
        print(f"Successfully created {out_bin} ({size:,} bytes)")
        print(f"Verified image: {verify_autoexec_bin(out_bin, key_path)}")


def _patch_state(binary: bytes, patches) -> str:
    states = []
    for patch in patches:
        actual = binary[patch.offset:patch.offset + len(patch.replacement)]
        if actual == patch.replacement:
            states.append("patched")
        elif actual == patch.expected:
            states.append("stock")
        else:
            states.append("unknown")
    return states[0] if len(set(states)) == 1 else "mixed/unknown"


def cmd_audit_usb(args):
    target = pathlib.Path(args.target_drive)
    image = target / "autoexec.bin" if target.is_dir() else target
    key_path = get_key_path(args.key)
    failures = []

    meta = verify_autoexec_bin(image, key_path)
    script = extract_autoexec_file(image, key_path, "/autoexec.sh")
    rbp = extract_autoexec_file(image, key_path, "/rbp.patched")
    try:
        md5_manifest = extract_autoexec_file(image, key_path, "/rbp.patched.md5").decode("ascii").split()[0]
    except Exception:
        md5_manifest = ""

    rbp_md5 = hashlib.md5(rbp).hexdigest()
    shebang_ok = script.startswith(b"#!/bin/sh\n")
    manifest_ok = bool(md5_manifest) and md5_manifest == rbp_md5
    if not shebang_ok:
        failures.append("autoexec.sh lacks a BOM-free shebang")
    if not manifest_ok:
        failures.append("rbp MD5 manifest is missing or mismatched")

    print(f"IMAGE: valid volume={meta['volume_name']} bytes={meta['size_bytes']}")
    print(f"LOADER: {'PASS' if shebang_ok else 'FAIL'} BOM-free shebang")
    print(f"RBP: md5={rbp_md5} manifest={'PASS' if manifest_ok else 'FAIL'}")
    print(f"PATCH waveform_3band: {_patch_state(rbp, WAVEFORM_PATCHES)}")
    print(f"PATCH deck_select_4deck: {_patch_state(rbp, DECK_PATCHES)}")
    print(f"PATCH settings_model_marker: {_patch_state(rbp, SETTINGS_MARKER_PATCHES)}")

    try:
        gui = extract_autoexec_file(image, key_path, "/gui/imagedata.dat")
        gui_manifest = extract_autoexec_file(image, key_path, "/gui/imagedata.dat.md5").decode("ascii").split()[0]
        gui_md5 = hashlib.md5(gui).hexdigest()
        gui_ok = gui_md5 == gui_manifest and b"GUI_RUNTIME_PROOF:" in script
        print(f"GUI: md5={gui_md5} manifest={'PASS' if gui_ok else 'FAIL'} runtime-loader={'PASS' if b'GUI_RUNTIME_PROOF:' in script else 'FAIL'}")
        if not gui_ok:
            failures.append("GUI payload manifest or runtime loader is invalid")
    except Exception:
        print("GUI: not included")

    try:
        helper = extract_autoexec_file(image, key_path, "/tools/xz-fb-overlay")
        print(f"FB HELPER: included bytes={len(helper)} md5={hashlib.md5(helper).hexdigest()}")
    except Exception:
        print("FB HELPER: not included")

    try:
        hook = extract_autoexec_file(image, key_path, "/tools/libxz-directfb-hook.so")
        hook_manifest = extract_autoexec_file(image, key_path, "/tools/libxz-directfb-hook.so.md5").decode("ascii").split()[0]
        hook_md5 = hashlib.md5(hook).hexdigest()
        hook_ok = hook_md5 == hook_manifest and b'LD_PRELOAD="$HOOK_RAM"' in script
        print(f"VJ FILMSTRIP HOOK: md5={hook_md5} manifest={'PASS' if hook_ok else 'FAIL'} loader={'PASS' if b'LD_PRELOAD=\"$HOOK_RAM\"' in script else 'FAIL'}")
        if not hook_ok:
            failures.append("VJ filmstrip hook manifest or runtime loader is invalid")
    except Exception:
        print("VJ FILMSTRIP HOOK: not included")

    session_path = target / "XZ_RUNTIME/session.txt" if target.is_dir() else image.parent / "XZ_RUNTIME/session.txt"
    session = session_path.read_text(encoding="utf-8", errors="replace") if session_path.is_file() else ""
    runtime_ok = (
        f"md5={rbp_md5} expected={rbp_md5}" in session
        and "SUCCESS: Patched rbp survived and its executable hash matches the payload." in session
    )
    print(f"RUNTIME: {'PASS' if runtime_ok else 'FAIL'} patched executable survival/hash proof")
    if not runtime_ok:
        failures.append("hardware session lacks patched-executable runtime proof")

    if failures:
        print("AUDIT: FAIL - " + "; ".join(failures))
        raise SystemExit(1)
    print("AUDIT: PASS")


def main():
    parser = argparse.ArgumentParser(description="Pioneer DJ XDJ-XZ Firmware & Mod Utility")
    sub = parser.add_subparsers(dest="command")

    build_p = sub.add_parser("build-usb", help="Build autoexec.bin to a target USB drive")
    build_p.add_argument("target_drive", help=r"Path to USB drive root (for example G:\)")
    build_p.add_argument("--key", help="Path to aes256.key")
    build_p.add_argument("--stock-rbp", help="Path to stock rbp binary")
    build_p.add_argument("--telnet", action=argparse.BooleanOptionalAction, default=True, help="Enable telnet on TCP 2323")
    build_p.add_argument("--vj-bridge-marker", action="store_true", help="Include the not-yet-implemented VJ bridge marker")
    build_p.add_argument("--no-3band", action="store_true", help="Disable the 3-band waveform patch")
    build_p.add_argument("--no-4deck", action="store_true", help="Disable the experimental 4-deck handler gate")
    build_p.add_argument("--no-watermark", action="store_true", help=argparse.SUPPRESS)
    build_p.add_argument("--gui-pack", help="Path to a same-layout custom imagedata.dat pack")
    build_p.add_argument("--fb-overlay-helper", help="Path to the static ARM framebuffer helper to stage in RAM")
    build_p.add_argument("--directfb-hook", help="Path to the ARM libxz-directfb-hook.so VJ.Tools receiver")
    build_p.add_argument("--gui-ip-patch", help="Path to the ARM runtime logo IP patcher")

    audit_p = sub.add_parser("audit-usb", help="Audit an image and captured hardware runtime proof")
    audit_p.add_argument("target_drive", help="USB root or path to autoexec.bin")
    audit_p.add_argument("--key", help="Path to aes256.key")

    args = parser.parse_args()
    if args.command == "build-usb":
        cmd_build_usb(args)
    elif args.command == "audit-usb":
        cmd_audit_usb(args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
