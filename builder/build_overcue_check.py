"""Build the desktop verifier from the same decoder used on the XZ."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.zig.resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-Wno-misleading-indentation', '-DMINIZ_NO_ARCHIVE_APIS', '-DMINIZ_NO_DEFLATE_APIS',
               str(root/'builder/overcue_check.c'), str(root/'builder/overcue_windows.c'),
               str(root/'mods/audio/vendor/miniz/miniz_tinfl.c'),
               str(root/'mods/audio/vendor/sha256/sha256.c'), '-lm', '-o', str(args.output)]
    if os.name == 'nt':
        command.append('-lshell32')
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
