"""Run the portable native LED packet regression without touching hardware."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-led-packet-') as folder:
    binary = Path(folder) / 'test.exe'
    subprocess.run([
        str(Path(args.zig).resolve()), 'cc', '-DXZ_LED_PORTABLE_TEST', '-UNDEBUG',
        '-std=c11', '-Wall', '-Wextra', '-Werror',
        str(root / 'native_led.c'), str(root / 'test_native_led.c'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
