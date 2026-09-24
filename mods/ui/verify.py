"""Run portable UI, touch and pad tests with live assertions."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent
common = [str(Path(args.zig).resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror']
with tempfile.TemporaryDirectory(prefix='xz-ui-test-') as folder:
    for name, sources in {
        'pads': ['stem_pads.c', 'test_stem_pads.c'],
        'pad-order': ['ui.c', 'stem_pads.c', 'test_pad_order.c'],
        'mixer-eq': ['mixer_eq.c', 'test_mixer_eq.c'],
        'native-mixer-eq': ['mixer_eq.c', 'test_native_mixer_eq.c'],
        'ui': ['ui.c', 'test_ui.c'],
        'touch': ['ui.c', 'native_touch.c', 'test_native_touch.c'],
        'wave': ['wave_viewport.c', 'test_wave_viewport.c'],
        'native-wave': ['native_wave.c', 'wave_viewport.c', 'test_native_wave.c'],
        'inline': ['ui.c', 'test_inline_stems.c'],
        'font': ['ui.c', 'test_font.c'],
    }.items():
        output = Path(folder) / (name + '.exe')
        subprocess.run(common + ['-UNDEBUG'] + [str(root / p) for p in sources] + ['-o', str(output)], check=True)
        subprocess.run([str(output)], check=True)
    negative = subprocess.run(common + ['-DNDEBUG', '-c', str(root / 'test_stem_pads.c'), '-o', str(Path(folder) / 'negative.o')], capture_output=True, text=True)
    if negative.returncode == 0 or 'Pad acceptance requires active assertions' not in negative.stderr:
        raise RuntimeError('Missing assertion rejection')
print('PASS assertion rejection')
