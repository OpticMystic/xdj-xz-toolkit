"""Save fresh pad events before unrelated controls overwrite the device ring."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('receipt',type=Path)
p.add_argument('binary',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--seconds',type=int,default=60)
a=p.parse_args()
if not 1<=a.seconds<=180:raise ValueError('Capture duration must be 1..180 seconds')
if a.output.exists():raise ValueError('Choose a new evidence file')
reader=Path(__file__).with_name('read_pad_trace.py')
records=[];last=None;pid=None;start=time.monotonic();samples=0
with tempfile.TemporaryDirectory(prefix='xz-pad-watch-') as temporary:
    while time.monotonic()-start<a.seconds:
        snapshot=Path(temporary)/f'{samples}.json'
        subprocess.run([sys.executable,str(reader),str(a.receipt),str(a.binary),str(snapshot)],stdout=subprocess.DEVNULL,check=True)
        data=json.loads(snapshot.read_text());samples+=1
        if pid is not None and data['pid']!=pid:raise RuntimeError('Player restarted during capture')
        pid=data['pid']
        if last is None:
            last=data['count']-1
            print(f'Watching PID {pid} after event {last}',flush=True)
        for row in data['records']:
            if row['ordinal']>last and (row['pad']<8 or row['key']=='0x4114'):
                records.append(row);print(json.dumps(row),flush=True)
        last=max(last,data['count']-1)
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps({'pid':pid,'samples':samples,'last_event':last,'records':records},indent=2)+'\n')
        presses=sum(r['key']=='0x4119' and r['operation']==0 for r in records)
        releases=sum(r['key']=='0x4119' and r['operation'] in (2,3) for r in records)
        if presses>=2 and releases>=2:break
        time.sleep(.3)
print(f'Saved {len(records)} fresh pad/mode events to {a.output}',flush=True)
