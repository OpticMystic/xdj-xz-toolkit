"""Shared runtime-payload staging for XDJ-XZ USB images."""

from __future__ import annotations

import hashlib
import pathlib
from collections.abc import Iterable


ROOT_DIR = pathlib.Path(__file__).resolve().parents[2]


def write_runtime_payload(
    staging_dir: pathlib.Path,
    rbp_data: bytes,
    *,
    features: Iterable[str],
    enable_telnet: bool = True,
    enable_vj_bridge_marker: bool = False,
    orchestrator_path: pathlib.Path | None = None,
    gui_pack_path: pathlib.Path | None = None,
    fb_overlay_helper_path: pathlib.Path | None = None,
    directfb_hook_path: pathlib.Path | None = None,
    gui_ip_patch_path: pathlib.Path | None = None,
    mods_runtime_path: pathlib.Path | None = None,
    mods_mode: str = "observer",
) -> dict[str, str]:
    """Write one canonical, self-verifying runtime payload staging tree."""

    staging = pathlib.Path(staging_dir)
    if mods_mode not in ("observer", "experimental"):
        raise ValueError("Unknown standalone mod mode")
    if mods_runtime_path is not None and directfb_hook_path is None:
        raise ValueError("Standalone mod payloads require an explicit receiver")
    staging.mkdir(parents=True, exist_ok=True)
    orchestrator = orchestrator_path or ROOT_DIR / "tools" / "xz_runtime" / "orchestrator.sh"

    script = orchestrator.read_bytes()
    if script.startswith(b"\xef\xbb\xbf"):
        script = script[3:]
    script = script.replace(b"\r\n", b"\n")
    if not script.startswith(b"#!/bin/sh\n"):
        raise ValueError(f"Orchestrator must start with a BOM-free POSIX shebang: {orchestrator}")

    rbp_md5 = hashlib.md5(rbp_data).hexdigest()
    feature_list = tuple(dict.fromkeys(features))

    (staging / "autoexec.sh").write_bytes(script)
    (staging / "rbp.patched").write_bytes(rbp_data)
    (staging / "rbp.patched.md5").write_text(
        f"{rbp_md5}  rbp.patched\n", encoding="ascii", newline="\n"
    )
    (staging / "build-info.txt").write_text(
        "XZMOD_PAYLOAD_VERSION=3\n"
        f"RBP_MD5={rbp_md5}\n"
        f"FEATURES={','.join(feature_list)}\n",
        encoding="ascii",
        newline="\n",
    )

    if enable_telnet:
        (staging / "enable_telnet").write_text("1\n", encoding="ascii", newline="\n")
    if enable_vj_bridge_marker:
        (staging / "enable_vj_bridge").write_text("1\n", encoding="ascii", newline="\n")

    gui_md5 = ""
    if gui_pack_path is not None:
        gui_data = pathlib.Path(gui_pack_path).read_bytes()
        gui_md5 = hashlib.md5(gui_data).hexdigest()
        gui_dir = staging / "gui"
        gui_dir.mkdir(parents=True, exist_ok=True)
        (gui_dir / "imagedata.dat").write_bytes(gui_data)
        (gui_dir / "imagedata.dat.md5").write_text(
            f"{gui_md5}  imagedata.dat\n", encoding="ascii", newline="\n"
        )

    helper_md5 = ""
    if fb_overlay_helper_path is not None:
        helper_data = pathlib.Path(fb_overlay_helper_path).read_bytes()
        helper_md5 = hashlib.md5(helper_data).hexdigest()
        tools_dir = staging / "tools"
        tools_dir.mkdir(parents=True, exist_ok=True)
        (tools_dir / "xz-fb-overlay").write_bytes(helper_data)

    directfb_hook_md5 = ""
    if directfb_hook_path is not None:
        hook_data = pathlib.Path(directfb_hook_path).read_bytes()
        directfb_hook_md5 = hashlib.md5(hook_data).hexdigest()
        tools_dir = staging / "tools"
        tools_dir.mkdir(parents=True, exist_ok=True)
        (tools_dir / "libxz-directfb-hook.so").write_bytes(hook_data)
        (tools_dir / "libxz-directfb-hook.so.md5").write_text(
            f"{directfb_hook_md5}  libxz-directfb-hook.so\n", encoding="ascii", newline="\n"
        )

    gui_ip_patch_md5 = ""
    if gui_ip_patch_path is not None:
        patch_data = pathlib.Path(gui_ip_patch_path).read_bytes()
        gui_ip_patch_md5 = hashlib.md5(patch_data).hexdigest()
        tools_dir = staging / "tools"
        tools_dir.mkdir(parents=True, exist_ok=True)
        (tools_dir / "xz-gui-ip-patch").write_bytes(patch_data)
        (tools_dir / "xz-gui-ip-patch.md5").write_text(
            f"{gui_ip_patch_md5}  xz-gui-ip-patch\n", encoding="ascii", newline="\n"
        )

    mods_md5 = ""
    if mods_runtime_path is not None:
        mods_data = pathlib.Path(mods_runtime_path).read_bytes()
        mods_md5 = hashlib.md5(mods_data).hexdigest()
        tools_dir = staging / "tools"
        tools_dir.mkdir(parents=True, exist_ok=True)
        (tools_dir / "libxz-mods.so").write_bytes(mods_data)
        (tools_dir / "libxz-mods.so.md5").write_text(f"{mods_md5}  libxz-mods.so\n", encoding="ascii", newline="\n")
        (staging / "mods-mode").write_text(mods_mode + "\n", encoding="ascii", newline="\n")

    return {
        "rbp_md5": rbp_md5,
        "gui_md5": gui_md5,
        "fb_overlay_helper_md5": helper_md5,
        "directfb_hook_md5": directfb_hook_md5,
        "gui_ip_patch_md5": gui_ip_patch_md5,
        "mods_md5": mods_md5,
        "features": ",".join(feature_list),
    }
