"""Build the desktop-only audio helper using the repository's pinned dr_libs."""
import argparse
from pathlib import Path
import os
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--zig", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--dr-libs", type=Path, default=Path(__file__).resolve().parents[1] / "mods/audio/vendor/dr_libs")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.zig.resolve()), "cc", "-O2", str(Path(__file__).with_name("audio_helper.c")),
               "-I", str(args.dr_libs), "-o", str(args.output)]
    if os.name == "nt":
        command.append("-lshell32")
    subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
