"""Run cue acceptance with assertions and prove the disabled-assert build is rejected."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--zig',required=True)
a=p.parse_args()
zig=str(Path(a.zig).resolve())
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-cue-test-') as tmp:
    exe=Path(tmp)/'test-cue.exe'
    common=[zig,'cc','-O2','-std=c11','-Wall','-Wextra','-Werror']
    negative=subprocess.run(common+['-DNDEBUG','-c',str(root/'test_cue.c'),'-o',str(Path(tmp)/'negative.o')],capture_output=True,text=True)
    if negative.returncode==0 or 'Cue acceptance requires active assertions' not in negative.stderr:
        raise RuntimeError('Disabled assertion build did not fail at the required guard')
    subprocess.run(common+['-UNDEBUG',str(root/'cue.c'),str(root/'test_cue.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS negative assertion compile gate')
