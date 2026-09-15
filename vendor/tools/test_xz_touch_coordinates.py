"""Replay physical XDJ taps through the hook's C coordinate conversion.

Run with Python on Linux/WSL and gcc. Fixtures are three ordered center taps
captured on 2026-09-05: half tempo, TAG, and Match (pad 8).
"""
import ctypes
import pathlib
import subprocess
import tempfile


def main():
    source = (pathlib.Path(__file__).parent / "xz_runtime/xz_directfb_hook.c").read_text()
    constants = "\n".join(line for line in source.splitlines() if line.startswith("#define XZ_TOUCH_RAW_"))
    conversion = source.split("if (fd == raw_touch_fd && result >= 6) {", 1)[1].split("touch_x = x;", 1)[0]
    program = "#include <stdint.h>\n" + constants
    program += "\nvoid decode(const void *buffer, int *out_x, int *out_y) {\n"
    program += conversion + "*out_x = x; *out_y = y; }\n"
    fixtures = [
        ("half press", "01 00 35 0f 3f 07", (12, 110, 50, 138)),
        ("half release", "00 00 39 0f f7 06", (12, 110, 50, 138)),
        ("TAG press", "01 00 a8 03 c9 09", (728, 217, 786, 265)),
        ("TAG release", "00 00 96 03 d8 09", (728, 217, 786, 265)),
        ("Match press", "01 00 de 03 16 0c", (699, 304, 794, 356)),
        ("Match release", "00 00 db 03 1e 0c", (699, 304, 794, 356)),
    ]
    with tempfile.TemporaryDirectory(prefix="xz-touch-") as temp:
        root = pathlib.Path(temp)
        (root / "decode.c").write_text(program)
        subprocess.run(["gcc", "-shared", "-fPIC", "-o", str(root / "decode.so"), str(root / "decode.c")], check=True)
        decode = ctypes.CDLL(str(root / "decode.so")).decode
        decode.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
        failures = []
        for name, raw, (left, top, right, bottom) in fixtures:
            x, y = ctypes.c_int(), ctypes.c_int()
            decode(ctypes.create_string_buffer(bytes.fromhex(raw)), ctypes.byref(x), ctypes.byref(y))
            ok = left <= x.value < right and top <= y.value < bottom
            print(f"{'PASS' if ok else 'FAIL'} {name}: ({x.value}, {y.value})")
            if not ok:
                failures.append(name)
        assert not failures, f"Physical taps missed their buttons: {failures}"


if __name__ == "__main__":
    main()
