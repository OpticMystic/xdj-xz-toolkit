"""Compile upstream's original cache key/meta writer as an independent oracle.

Run on POSIX: python3 verify_upstream.py /path/to/pinned/cdj3k-mods
No device access. All generated files live in a temporary directory.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

PIN = "e74e199603e2a25567950ca72997c38d17ada4e8"
HERE = Path(__file__).resolve().parent


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def run(args):
    return subprocess.check_output([str(x) for x in args], text=True).strip()


def main():
    upstream = Path(sys.argv[1]).resolve()
    assert run(["git", "-c", f"safe.directory={upstream}", "-C", upstream,
                "rev-parse", "HEAD"]) == PIN
    source = run(["git", "-c", f"safe.directory={upstream}", "-C", upstream,
                  "show", f"{PIN}:package/deck/mods/stem/cache.c"])
    prefix = """#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#define FNV64_OFFSET 1469598103934665603ull
#define FNV64_PRIME 1099511628211ull
#define KEY_WINDOW (64 * 1024)
#define STEM_CACHE_PATH_MAX 4096
#define META_VERSION 1
"""
    oracle = prefix + "\n".join(function(source, name) for name in [
        "static uint64_t fnv1a(", "static int key_of(", "static int meta_write("])
    oracle += """
int main(int argc,char **argv) {
 char key[17];
 if(argc==3) return meta_write(argv[2],12345678,0.5f,0.75f);
 if(key_of(argv[1],12345678,key,sizeof(key))) return 2;
 puts(key); return 0;
}
"""
    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory(prefix="xz-upstream-stems-") as tmp:
        root = Path(tmp)
        (root / "oracle.c").write_text(oracle)
        run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", root / "oracle.c", "-o", root / "oracle"])
        run([cc, "-std=c11", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-I", HERE.parent,
             HERE / "test_stems.c", HERE.parent / "stem_cache.c", HERE.parent / "stem_mix.c",
             "-lm", "-o", root / "tests"])
        print(run([root / "tests"]))
        # Both overlapping head/tail windows and the 64 KiB edge matter.
        for size in [1, 65535, 65536, 65537, 100000, 131072, 200003]:
            track = root / f"track-{size}.bin"
            track.write_bytes(bytes((i * 37 + i // 239) % 256 for i in range(size)))
            expected = run([root / "oracle", track])
            actual = run([root / "tests", "key", track])
            assert actual == expected, (size, actual, expected)
        # Sparse >4 GiB fixture catches off_t truncation without allocating it.
        track = root / "large-track.bin"
        with track.open("wb") as f:
            f.write(b"large-file-head")
            f.seek(2**32 + 789)
            f.write(b"large-file-tail")
        expected = run([root / "oracle", track])
        assert run([root / "tests", "key", track]) == expected
        directory = root / "mods/stemd-cache/test-model" / expected[:2] / expected
        directory.mkdir(parents=True)
        # Payload decoding is not tested by this cache lookup fixture.
        (directory / "harmonics.flac").write_bytes(b"fLaC lookup-only fixture")
        (directory / "vocals.wav").write_bytes(b"RIFF lookup-only fixture")
        run([root / "oracle", "meta", directory])
        run([root / "tests", "lookup", root, track, "valid"])
        meta = (directory / "meta").read_text()
        for invalid in [meta.replace("v=1", "v=2"), meta.replace("12345678", "12345679"),
                        meta.replace("0.5", "nan"), meta.replace("0.5", "0"),
                        meta + "vocals=0.75\n", meta.replace("harmonics=", "other="),
                        meta.replace("0.5", "0.5junk"), "v=1\n"]:
            (directory / "meta").write_text(invalid)
            run([root / "tests", "lookup", root, track, "invalid"])
        (directory / "meta").write_text(meta)
        (directory / "vocals.wav").unlink()
        run([root / "tests", "lookup", root, track, "invalid"])
        print("8 keys match compiled upstream; upstream meta accepted; 9 invalid entries rejected")


if __name__ == "__main__":
    main()
