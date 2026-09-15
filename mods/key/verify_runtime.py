"""Build and run the mocked XZ post-stretch key adapter."""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", default=shutil.which("zig"))
    args = parser.parse_args()
    if not args.zig:
        parser.error("Supply --zig")
    zig = shutil.which(args.zig) or str(Path(args.zig).resolve())
    with tempfile.TemporaryDirectory(prefix="xz-key-runtime-") as temporary:
        root = Path(temporary)
        common = [zig, "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"]
        negative = subprocess.run(common + ["-DNDEBUG", "-c", str(HERE / "test_runtime.c"),
                                  "-o", str(root / "negative.o")], capture_output=True, text=True)
        if negative.returncode == 0 or "active assertions" not in negative.stderr:
            raise RuntimeError("Disabled-assertion runtime build was not rejected")
        executable = root / "key-runtime-test.exe"
        subprocess.run(common + ["-UNDEBUG", str(HERE / "keyshift.c"),
                       str(HERE / "runtime.c"), str(HERE / "test_runtime.c"),
                       "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        for source in ("keyshift.c", "runtime.c"):
            subprocess.run([zig, "cc", "-target", "arm-linux-gnueabi.2.13",
                           "-mcpu=cortex_a9", "-std=c11", "-O2", "-Wall", "-Wextra",
                           "-Werror", "-fPIC", "-c", str(HERE / source),
                           "-o", str(root / (source + ".o"))], check=True)
    print("PASS mocked native post-hook and ARM soft-float compile")


if __name__ == "__main__":
    main()
