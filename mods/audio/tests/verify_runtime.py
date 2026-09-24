"""Build and run the actual audio runtime with mocked firmware and sanitizers.

Requirements: Linux host, Python 3, GCC or Clang, pthreads, ASan and UBSan.
Run: python3 packages/xdj-xz-toolkit/mods/audio/tests/verify_runtime.py
All generated executables are temporary. No firmware or device access occurs.
The enabled-hook fixture requires a private mmap allocation below 4 GiB.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
CC = shlex.split(os.environ.get("CC", "cc"))

with tempfile.TemporaryDirectory(prefix="xz-runtime-verification-") as temporary:
    root = Path(temporary)
    deliberate = root / "assertions.c"
    deliberate.write_text("#include <assert.h>\nint main(void){assert(0);return 0;}\n")
    subprocess.run(CC + ["-O2", "-UNDEBUG", str(deliberate), "-o", str(root / "assertions")], check=True)
    result = subprocess.run([str(root / "assertions")], cwd=root, capture_output=True)
    if result.returncode == 0:
        raise RuntimeError("Test compiler disabled assertions")
    print(f"Deliberate failing assertion rejected, exit {result.returncode}", flush=True)

    sources = [HERE / "test_runtime.c"] + [HERE.parent / name for name in (
        "native_reader.c", "stem_cache.c", "stem_mix.c", "stem_decode.c", "overcue_file.c", "overcue_stream.c",
        "vendor/miniz/miniz_tinfl.c", "vendor/sha256/sha256.c")]
    command = CC + ["-std=c11", "-O1", "-g", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    command += ["-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_DEFLATE_APIS", "-Wno-misleading-indentation"]
    command += [str(source) for source in sources]
    command += ["-pthread", "-lm", "-o", str(root / "runtime-tests")]
    subprocess.run(command, check=True)
    subprocess.run([str(root / "runtime-tests")], cwd=root, check=True, timeout=120)
