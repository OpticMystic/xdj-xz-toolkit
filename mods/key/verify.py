"""Build and run the portable key shifter without device access."""
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
    with tempfile.TemporaryDirectory(prefix="xz-key-shift-") as temporary:
        root = Path(temporary)
        common = [zig, "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"]
        negative = subprocess.run(common + ["-DNDEBUG", "-c", str(HERE / "test_keyshift.c"),
                                  "-o", str(root / "negative.o")], capture_output=True, text=True)
        if negative.returncode == 0 or "active assertions" not in negative.stderr:
            raise RuntimeError("Disabled-assertion acceptance build was not rejected")
        executable = root / "keyshift-test.exe"
        subprocess.run(common + ["-UNDEBUG", str(HERE / "keyshift.c"),
                       str(HERE / "test_keyshift.c"), "-lm", "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=120)
        subprocess.run([zig, "cc", "-target", "arm-linux-gnueabi.2.13",
                       "-mcpu=cortex_a9", "-std=c11", "-O2", "-Wall", "-Wextra",
                       "-Werror", "-fPIC", "-c", str(HERE / "keyshift.c"),
                       "-o", str(root / "keyshift-arm.o")], check=True)
    print("PASS assertion gate, host tone/bounds/state tests and ARM soft-float compile")


if __name__ == "__main__":
    main()
