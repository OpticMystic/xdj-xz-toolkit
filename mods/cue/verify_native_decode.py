"""Compile and run the real ARM input decoder in an isolated XZ test process."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from device_smoke import command
from live_io import HOST,transfer

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--zig',required=True)
p.add_argument('--evidence',type=Path,required=True)
a=p.parse_args()
root=Path(__file__).resolve().parent
if a.evidence.exists():raise ValueError('Choose a new evidence file')
with tempfile.TemporaryDirectory(prefix='xz-native-decode-') as folder:
    binary=Path(folder)/'test'
    subprocess.run([str(Path(a.zig).resolve()),'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9',
        '-O2','-UNDEBUG','-std=c11','-Wall','-Wextra','-Werror',str(root/'native.c'),str(root/'test_native_decode.c'),'-o',str(binary)],check=True)
    data=binary.read_bytes()
    remote='/dev/shm/xz-native-decode-'+uuid.uuid4().hex
    before=command(HOST,'pidof rbp').strip()
    transfer('receive',remote,len(data),data)
    try:
        result=command(HOST,f'chmod 700 {remote}; ulimit -c 0; {remote}; echo test_status=$?')
        after=command(HOST,'pidof rbp').strip()
        report={'sha256':hashlib.sha256(data).hexdigest(),'rbp_before':before,'rbp_after':after,'result':result}
        a.evidence.parent.mkdir(parents=True,exist_ok=True)
        a.evidence.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2))
        if before!=after or 'test_status=0' not in result:raise RuntimeError('Native decoder test failed')
    finally:
        command(HOST,f'rm -f {remote}')
