"""Explicitly restart the XZ application into a reviewed private RAM test image."""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import shlex
import sys
import time

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / "vendor"))
from tools.xz_firmware.image_builder import extract_autoexec_file
from tools.xz_nand_dump import open_telnet
from device_smoke import command, upload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--host", default="169.254.168.59")
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--previous-evidence", type=Path, help="Verified live receipt when replacing an earlier development pair")
    parser.add_argument("--previous-bundle", type=Path, help="Verified bundle used by the normal combined USB loader")
    parser.add_argument("--fast-transfer", action="store_true", help="Use the already installed private RAM transfer helper")
    parser.add_argument("--pad-trace", action="store_true", help="Enable bounded in-memory physical-key diagnostics")
    parser.add_argument("--native-view", action="store_true", help="Start this RAM trial on the native deck view; the visible VJ.Tools button can reopen the stream")
    parser.add_argument("--stems", action="store_true", help="Enable prepared-stem discovery with native-audio alignment")
    parser.add_argument("--ram-settings", action="store_true", help="Read existing preferences but keep all trial changes in RAM")
    parser.add_argument("--inline-observe", action="store_true", help="Observe the exact native wave lock without drawing")
    parser.add_argument("--settings-usb", help="Existing mounted XZ USB volume for persistent preferences")
    args = parser.parse_args()
    if args.settings_usb:
        if not re.fullmatch(r"/media/usb[12]/[A-Za-z0-9_-]+",args.settings_usb):
            raise ValueError("Expected an explicit XZ USB mount")
        mounts = command(args.host,"cat /proc/mounts")
        if not any(len(line.split()) >= 3 and line.split()[1] == args.settings_usb and line.split()[2] == "vfat" for line in mounts.splitlines()):
            raise ValueError("Settings USB is not currently mounted as FAT")
    def stage(path, data):
        if args.fast_transfer:
            from live_io import HOST, transfer
            if args.host != HOST:
                raise ValueError("Private transfer helper is bound to the verified device address")
            transfer("receive", path, len(data), data)
        else:
            upload(args.host, path, data)
    manifest = json.loads((args.image.parent / "manifest.json").read_text())
    if hashlib.sha256(args.image.read_bytes()).hexdigest() != manifest["image_sha256"]:
        raise ValueError("Image does not match its reviewed manifest")
    if manifest["profile"] not in ("observer", "experimental"):
        raise ValueError("Unknown private development profile")
    before = command(args.host, r"""pid=$(pidof rbp); echo PID=$pid; echo EXE=$(readlink /proc/$pid/exe); md5sum /proc/$pid/exe; echo ARGS; tr '\000' '\n' </proc/$pid/cmdline; echo PRELOAD; tr '\000' '\n' </proc/$pid/environ | grep '^LD_PRELOAD='""")
    pid = re.search(r"^PID=(\d+)$", before, re.M).group(1)
    if "EXE=/root/pdj/rbp" not in before or manifest["payload"]["rbp_md5"] not in before:
        raise ValueError("Running application differs from the reviewed firmware")
    argv = before.split("ARGS\n", 1)[1].split("\nPRELOAD", 1)[0].splitlines()
    if argv[0] not in ("/root/pdj/rbp", "./rbp") or any(not re.fullmatch(r"-[A-Za-z0-9]+", a) for a in argv[1:]):
        raise ValueError("Unexpected application arguments; preserve them explicitly before restarting")
    match = re.search(r"^LD_PRELOAD=(.*)$", before, re.M)
    old_preload = match.group(1) if match else ""
    tokens = re.split(r"[ :]+", old_preload.strip()) if old_preload.strip() else []
    old_receiver = "/dev/shm/libxz-directfb-hook.so"
    old_mod = None
    old_enabled = old_observer = old_ui = 0
    if args.previous_evidence:
        previous = json.loads(args.previous_evidence.read_text())
        if not previous.get("after", "").startswith("STARTED\n" + pid + "\n") or previous.get("new_preload") != old_preload:
            raise ValueError("Previous receipt does not describe the running process and preload")
        previous_root = previous["remote"]
        if not re.fullmatch(r"/dev/shm/xz-dev-[0-9a-f]{12}-[0-9]+", previous_root):
            raise ValueError("Previous receipt is not an isolated development directory")
        old_mod = previous_root + "/mods.so"
        old_receiver = previous_root + "/receiver.so" if previous["mode"] == "experimental" else old_receiver
        if old_mod not in tokens or old_receiver not in tokens:
            raise ValueError("Previous development pair is not in the active preload")
        # Verify the current files against the locally retained, reviewed image hashes.
        for path, expected in ((old_mod, previous["runtime_sha256"]), (old_receiver, previous["receiver_sha256"])):
            data = base64.b64decode("".join(command(args.host, "base64 " + shlex.quote(path)).splitlines()), validate=True)
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError("Previous development library differs from its receipt")
        old_enabled = 1
        old_observer = int(previous["mode"] == "observer")
        old_ui = int(previous["mode"] == "experimental")
    elif "/dev/shm/libxz-mods.so" in tokens:
        if not args.previous_bundle:
            raise ValueError("Supply --previous-bundle before replacing a combined USB runtime")
        previous = json.loads((args.previous_bundle / "manifest.json").read_text())
        old_mod = "/dev/shm/libxz-mods.so"
        if old_receiver not in tokens:
            raise ValueError("Combined USB runtime has no matching receiver")
        for remote_file, name in ((old_mod, "libxz-mods.so"), (old_receiver, "libxz-receiver.so")):
            data = base64.b64decode("".join(command(args.host, "base64 " + shlex.quote(remote_file)).splitlines()), validate=True)
            if hashlib.sha256(data).hexdigest() != previous["files"][name]:
                raise ValueError("Running USB library differs from the previous bundle")
        old_enabled = old_ui = 1
    elif old_receiver not in tokens:
        raise ValueError("Expected the existing receiver; refusing an unknown preload composition")
    remote = "/dev/shm/xz-dev-" + manifest["runtime_sha256"][:12] + "-" + pid
    created = command(args.host, f"mkdir {remote}; echo created=$?")
    if "created=0" not in created:
        raise ValueError("This test directory already exists; inspect the previous run first")
    key = ROOT.parent / "vendor/keys/aes256.key"
    runtime = extract_autoexec_file(args.image, key, "/tools/libxz-mods.so")
    receiver = extract_autoexec_file(args.image, key, "/tools/libxz-directfb-hook.so")
    if hashlib.sha256(runtime).hexdigest() != manifest["runtime_sha256"] or hashlib.sha256(receiver).hexdigest() != manifest["receiver_sha256"]:
        raise ValueError("Image library hashes differ from the manifest")
    stage(remote + "/mods.so", runtime)
    stage(remote + "/receiver.so", receiver)
    mode = manifest["profile"]
    if old_mod:
        tokens = [p for p in tokens if p != old_mod]
    if mode == "experimental":
        tokens = [p for p in tokens if p != old_receiver] + [remote + "/receiver.so"]
    tokens += [remote + "/mods.so"]
    new_preload = ":".join(tokens)
    invocation = " ".join(shlex.quote(a) for a in ["/root/pdj/rbp"] + argv[1:])
    ip = command(args.host, "ifconfig eth0 | sed -n 's/.*inet addr:\\([0-9.]*\\).*/\\1/p' | head -1").strip()
    if ip != args.host or not ip.startswith("169.254."):
        raise ValueError("This test restarter expects the verified USB link-local connection")
    restore_network = f"ifconfig eth0 {ip} netmask 255.255.0.0 up; route add -net 169.254.0.0 netmask 255.255.0.0 dev eth0 2>/dev/null || true"
    old_env = "LD_PRELOAD=" + shlex.quote(old_preload) + f" XZ_MODS_ENABLE={old_enabled} XZ_MODS_OBSERVER={old_observer} XZ_MODS_UI={old_ui} XZ_MODS_STEMS=0 XZ_MODS_KEYSHIFT=0"
    new_env = "LD_PRELOAD=" + shlex.quote(new_preload) + f" XZ_MODS_ENABLE=1 XZ_MODS_OBSERVER={int(mode == 'observer')} XZ_MODS_UI={int(mode == 'experimental')} XZ_MODS_STEMS={int(args.stems)} XZ_MODS_GATE_CUE=0 XZ_MODS_SMART_CUE=0 XZ_MODS_KEYSHIFT=0"
    if args.inline_observe:
        new_env += " XZ_MODS_INLINE=observe"
    if args.stems:
        new_env += " XZ_MODS_STEMS_FORCE=1"
    if args.ram_settings:
        new_env += " XZ_MODS_SETTINGS_READONLY=1"
    if args.settings_usb:
        new_env += " XZ_MODS_USB=" + shlex.quote(args.settings_usb)
    new_env += f" XZ_MODS_PAD_TRACE={int(args.pad_trace)}"
    if args.native_view:
        new_env += " XZ_MODS_NATIVE_VIEW=1"
    script = f'''#!/bin/sh
trap '' HUP
ulimit -c 0
cd /root/pdj || exit 1
kill -TERM {pid} 2>/dev/null || true
i=0
while kill -0 {pid} 2>/dev/null && [ "$i" -lt 5 ]; do sleep 1; i=$((i+1)); done
if [ "$(readlink /proc/{pid}/exe 2>/dev/null)" = /root/pdj/rbp ]; then kill -KILL {pid}; fi
sleep 1
{new_env} {invocation} >{remote}/application.log 2>&1 &
new_pid=$!
echo "$new_pid" >{remote}/pid
sleep 6
{restore_network}
if kill -0 "$new_pid" 2>/dev/null; then
 echo STARTED >{remote}/status
else
 {old_env} {invocation} >{remote}/rollback.log 2>&1 &
 echo "$!" >{remote}/rollback-pid
 sleep 5
 {restore_network}
 echo ROLLED_BACK >{remote}/status
fi
'''
    rollback = f'''#!/bin/sh
trap '' HUP
cd /root/pdj || exit 1
test_pid=$(cat {remote}/pid)
if [ "$(readlink /proc/$test_pid/exe 2>/dev/null)" = /root/pdj/rbp ]; then kill -TERM "$test_pid"; sleep 3; [ "$(readlink /proc/$test_pid/exe 2>/dev/null)" = /root/pdj/rbp ] && kill -KILL "$test_pid"; fi
{old_env} {invocation} >{remote}/rollback.log 2>&1 &
echo "$!" >{remote}/rollback-pid
sleep 6
{restore_network}
'''
    stage(remote + "/restart.sh", script.encode())
    stage(remote + "/rollback.sh", rollback.encode())
    receipt = {"before": before, "remote": remote, "mode": mode, "runtime_sha256": manifest["runtime_sha256"], "receiver_sha256": manifest["receiver_sha256"], "old_preload": old_preload, "new_preload": new_preload}
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"Restarting only rbp PID {pid}; rollback script {remote}/rollback.sh", flush=True)
    with open_telnet(args.host, source_address=("169.254.168.58", 0) if args.host == "169.254.168.59" else None) as shell:
        shell.sendall(f"sh {remote}/restart.sh </dev/null >{remote}/restart.log 2>&1 &\r\n".encode())
        deadline = time.monotonic() + 18
        while time.monotonic() < deadline:
            try: shell.recv(4096)
            except TimeoutError: pass
            except ConnectionError: break
    deadline = time.monotonic() + 30
    after = ""
    while time.monotonic() < deadline:
        try:
            after = command(args.host, f"cat {remote}/status; pidof rbp; tail -8 {remote}/application.log; tail -8 /dev/shm/xz-mods.log")
            if after.startswith(("STARTED", "ROLLED_BACK")): break
        except OSError:
            pass
        time.sleep(1)
    receipt["after"] = after
    args.evidence.write_text(json.dumps(receipt, indent=2) + "\n")
    print(after)
    if not after.startswith("STARTED"):
        raise RuntimeError("Development application did not stay running; inspect rollback evidence")


if __name__ == "__main__":
    main()
