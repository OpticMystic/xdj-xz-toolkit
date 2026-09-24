"""Build and inspect the XZ ARM development runtime without deploying it."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
from elftools.elf.elffile import ELFFile

ROOT = pathlib.Path(__file__).resolve().parent


def inspect(path: pathlib.Path) -> dict:
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf["e_machine"] != "EM_ARM":
            raise ValueError("Expected ELF32 little-endian ARM")
        if elf["e_flags"] & 0x400:
            raise ValueError("Hard-float calling convention does not match the XZ application")
        versions = set()
        section = elf.get_section_by_name(".gnu.version_r")
        if section:
            for _, auxiliaries in section.iter_versions():
                versions.update(aux.name for aux in auxiliaries)
        for version in versions:
            if version.startswith("GLIBC_"):
                if tuple(map(int, version[6:].split("."))) > (2, 4):
                    raise ValueError(f"XZ ABI gate failed: {version} exceeds verified GLIBC_2.4 imports")
        libraries = []
        dynamic = elf.get_section_by_name(".dynamic")
        if dynamic:
            libraries = [tag.needed for tag in dynamic.iter_tags() if tag.entry.d_tag == "DT_NEEDED"]
            tags = {tag.entry.d_tag: tag.entry.d_val for tag in dynamic.iter_tags()}
            if tags.get("DT_RELCOUNT", 0) and tags.get("DT_JMPREL"):
                if tags.get("DT_REL", 0) + tags.get("DT_RELSZ", 0) != tags["DT_JMPREL"]:
                    raise ValueError("XZ glibc 2.13 needs contiguous REL and PLT relocation tables")
        nodelete = bool(dynamic and any(tag.entry.d_tag == "DT_FLAGS_1" and tag.entry.d_val & 8
                                       for tag in dynamic.iter_tags()))
        if elf["e_type"] == "ET_DYN" and not nodelete:
            raise ValueError("Hook library must remain mapped after dlclose")
        allowed = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "ld-linux.so.3"}
        if set(libraries) - allowed:
            raise ValueError(f"Unexpected runtime dependencies: {libraries}")
        imports = [s.name for s in elf.get_section_by_name(".dynsym").iter_symbols()
                   if s.name and s["st_shndx"] == "SHN_UNDEF"]
        return {"architecture": "ARM32 soft-float ABI", "libraries": libraries,
                "symbol_versions": sorted(versions), "imports": sorted(imports),
                "nodelete": nodelete, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", default=shutil.which("zig"))
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if not args.zig:
        parser.error("Supply --zig with a Zig executable supporting arm-linux-gnueabi.2.13")
    args.output.mkdir(parents=True, exist_ok=True)
    destination = args.output.resolve() / "libxz-mods-development.so"
    compiler = shutil.which(args.zig) or str(pathlib.Path(args.zig).resolve())
    # Keep the qualified legacy decoder compilation unchanged. LLVM 21 crashes
    # when compiling its dr_libs paths with softfp. Only the new streaming math
    # needs VFP; softfp retains the same function-call ABI as the legacy objects.
    stream_object = args.output.resolve() / "overcue-stream.o"
    subprocess.run([compiler, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
                    "-mfloat-abi=softfp", "-mfpu=neon", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fPIC", "-fvisibility=hidden", "-c", str(ROOT / "audio/overcue_stream.c"),
                    "-o", str(stream_object)], check=True)
    command = [compiler, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
               "-O2", "-s", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared",
               "-fvisibility=hidden", "-Wl,--no-undefined", "-Wl,-z,nodelete",
               "-Wl,-T," + str(ROOT / "xz-relocations.ld"),
               str(ROOT / "runtime.c"), str(ROOT / "cue/cue.c"), str(ROOT / "cue/native.c"),
               str(ROOT / "ui_runtime.c"), str(ROOT / "ui/ui.c"), str(ROOT / "ui/native_touch.c"),
               str(ROOT / "ui/stem_pads.c"),
               str(ROOT / "ui/native_led.c"),
               str(ROOT / "ui/native_wave.c"),
               str(ROOT / "ui/native_wave_runtime.c"),
               str(ROOT / "settings.c"),
               str(ROOT / "ui/wave_viewport.c"),
               str(ROOT / "key/keyshift.c"), str(ROOT / "key/runtime.c"),
               str(ROOT / "audio/runtime.c"), str(ROOT / "audio/native_reader.c"),
               str(ROOT / "audio/stem_cache.c"), str(ROOT / "audio/stem_mix.c"), str(ROOT / "audio/stem_decode.c"),
               str(ROOT / "audio/overcue_file.c"), str(stream_object),
               str(ROOT / "audio/vendor/miniz/miniz_tinfl.c"), str(ROOT / "audio/vendor/sha256/sha256.c"),
               "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_DEFLATE_APIS",
               "-pthread", "-lm", "-ldl",
               "-o", str(destination)]
    subprocess.run(command, check=True)
    result = inspect(destination)
    vendor = ROOT.parent / "vendor"
    receiver = args.output.resolve() / "libxz-directfb-mods-test.so"
    subprocess.run([compiler, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
                    "-O2", "-s", "-Wall", "-Wextra", "-Werror", "-std=gnu99", "-fPIC", "-shared",
                    "-Wl,-z,nodelete", "-Wl,-T," + str(ROOT / "xz-relocations.ld"),
                    "-I" + str(vendor / "build/dfb-generated"),
                    "-I" + str(vendor / "build/directfb-1.4-src/include"),
                    "-I" + str(vendor / "build/directfb-1.4-src/lib"),
                    str(vendor / "tools/xz_runtime/xz_directfb_hook.c"), "-ldl", "-pthread",
                    "-o", str(receiver)], check=True)
    result["display_bridge"] = inspect(receiver)
    common = [compiler, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
              "-O2", "-UNDEBUG", "-s", "-std=c11", "-Wall", "-Wextra", "-Werror"]
    tests = {
        "runtime-smoke": [str(ROOT / "runtime_smoke.c"), "-ldl"],
        "cue-test": [str(ROOT / "cue/cue.c"), str(ROOT / "cue/test_cue.c")],
        "settings-test": [str(ROOT / "settings.c"), str(ROOT / "tests/test_settings.c")],
        "native-decode-test": [str(ROOT / "cue/native.c"), str(ROOT / "cue/test_native_decode.c")],
        "stem-test": ["-I" + str(ROOT / "audio"), str(ROOT / "audio/stem_cache.c"),
                      str(ROOT / "audio/stem_mix.c"), str(ROOT / "audio/tests/test_stems.c"), "-lm"],
        "decode-test": ["-I" + str(ROOT / "audio"), str(ROOT / "audio/stem_decode.c"),
                        str(ROOT / "audio/tests/test_decode.c"), "-lm"],
    }
    test_evidence = {}
    for name, sources in tests.items():
        binary = args.output.resolve() / name
        subprocess.run(common + sources + ["-o", str(binary)], check=True)
        test_evidence[name] = inspect(binary)
    result.update({"artifact": destination.name, "development_only": True,
                   "default_enabled": False, "hardware_verified": False,
                   "features": {"gate_cue": "native adapter, unverified",
                                "smart_cue": "native adapter, unverified",
                                "stems": "OverCue v4 96000 Hz paged PCM and legacy 44100 Hz WAV/FLAC; physical acceptance pending",
                                "ui": "render and touch adapters, physical acceptance pending",
                                "vj_tools": "optional display bridge; original receiver separately packaged"}})
    destination.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
    (args.output / "tests-build.json").write_text(json.dumps(test_evidence, indent=2) + "\n", encoding="utf8")
    print(f"Built {destination.name}: {result['sha256']}")
    print(f"Display bridge: {result['display_bridge']['sha256']}; ARM32 / GLIBC_2.4 / NODELETE checks passed")
    print(f"Metadata: {destination.with_suffix('.json')}")


if __name__ == "__main__":
    main()
