"""Build the standalone XZ Mods pair for the Library USB loader."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

REPO = Path(__file__).resolve().parents[1]
PACKAGE = REPO / "packages/xdj-xz-toolkit"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Standalone xdj-xz-toolkit checkout")
    parser.add_argument("--zig", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="Candidate bundle directory; promote after hardware verification")
    parser.add_argument("--build-output", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()
    subprocess.run([sys.executable, str(source / "mods/build.py"), "--zig", str(args.zig.resolve()),
                    "--output", str(args.build_output.resolve())], check=True)
    # Stage only redistributable source and the two libraries. No firmware or keys.
    with tempfile.TemporaryDirectory(prefix="xz-mods-bundle-") as temporary:
        output = Path(temporary)
        subprocess.run([sys.executable, str(source/'mods/ui/build_branding_assets.py'), str(source), str(output/'branding')],check=True)
        for original, name in (("libxz-mods-development.so", "libxz-mods.so"),
                               ("libxz-directfb-mods-test.so", "libxz-receiver.so")):
            shutil.copyfile(args.build_output / original, output / name)
        bootstrap = (source / "vendor/tools/xz_runtime/orchestrator.sh").read_bytes().replace(b"\r\n", b"\n")
        (output / "bootstrap.sh").write_bytes(bootstrap)
        licenses = output / "licenses"
        licenses.mkdir()
        for original, name in (("mods/key/LICENSE-MPL-2.0", "Mozilla-MPL-2.0.txt"),
                               ("mods/ui/fonts/OFL.txt", "Barlow-OFL.txt"),
                               ("mods/audio/licenses/LICENSE-MIT", "cdj3k-mods-MIT.txt"),
                               ("mods/audio/licenses/LICENSE-APACHE", "cdj3k-mods-APACHE.txt"),
                               ("mods/audio/vendor/dr_libs/LICENSE", "dr-libs.txt")):
            shutil.copyfile(source / original, licenses / name)
        for original, name in (("mods/audio/vendor/miniz/LICENSE", "miniz-MIT.txt"),
                               ("mods/audio/vendor/jsmn/LICENSE", "jsmn-MIT.txt"),
                               ("mods/audio/vendor/sha256/README.md", "sha256-public-domain.txt")):
            if (source / original).is_file():
                shutil.copyfile(source / original, licenses / name)
        roots = [source / "mods", source / "vendor/tools/xz_gui", source / "vendor/build/dfb-generated",
                 source / "vendor/build/directfb-1.4-src/include", source / "vendor/build/directfb-1.4-src/lib"]
        files = [p for root in roots for p in root.rglob("*") if p.is_file() and
                 "__pycache__" not in p.parts and (p.suffix in (".c", ".h", ".py", ".ld", ".md", ".txt", ".ttf", ".json", ".in", ".png") or p.name.startswith(("LICENSE", "COPYING")))]
        files += [source / "vendor/tools/xz_runtime" / name for name in ("xz_directfb_hook.c", "mods_bridge.h", "orchestrator.sh")]
        with zipfile.ZipFile(output / "source.zip", "w", zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(set(files)):
                archive.write(path, path.relative_to(source).as_posix())
        commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        manifest = {"schema_version": 1, "firmware": "XDJ-XZ 1.26", "profile": "experimental",
                    "application_md5": "6a7ccb454e52afa26a73f3380706c9ca",
                    "source_repository": "https://github.com/OpticMystic/xdj-xz-toolkit",
                    "source_commit": commit, "hardware_qualified": False,
                    "source_dirty": bool(subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True).strip()),
                    "files": {p.relative_to(output).as_posix(): sha(p) for p in sorted(output.rglob("*")) if p.is_file()}}
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
        args.output.mkdir(parents=True, exist_ok=True)
        shutil.copytree(output, args.output, dirs_exist_ok=True)
    print(f"XZ Mods paired runtime bundle: {args.output}")


if __name__ == "__main__":
    main()
