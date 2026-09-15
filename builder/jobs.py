"""Bounded child-process execution with cooperative cancellation."""
from __future__ import annotations
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from contextlib import contextmanager

@contextmanager
def exclusive_lock(path):
    with Path(path).open('a+b') as handle:
        if handle.tell()==0:handle.write(b'0');handle.flush()
        handle.seek(0)
        try:
            if os.name=='nt':
                import msvcrt
                msvcrt.locking(handle.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(handle.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
        except OSError as exc:raise RuntimeError('Another XZ Mods window is using the separation engine') from exc
        try:yield
        finally:
            handle.seek(0)
            if os.name=='nt':msvcrt.locking(handle.fileno(),msvcrt.LK_UNLCK,1)
            else:fcntl.flock(handle.fileno(),fcntl.LOCK_UN)

class Cancelled(Exception):
    pass

class Job:
    def __init__(self,cancel_file=None):
        self.cancel_file=Path(cancel_file) if cancel_file else None
    def check(self):
        if self.cancel_file and self.cancel_file.exists():raise Cancelled('Cancelled')
    def progress(self,stage,message):
        self.check()
        print(json.dumps({'event':'progress','stage':stage,'message':message}),flush=True)
    def run(self,argv,*,cwd=None,env=None):
        self.check()
        with tempfile.TemporaryFile() as output:
            process=subprocess.Popen([str(x) for x in argv],cwd=cwd,env=env,
                stdin=subprocess.DEVNULL,stdout=output,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            try:
                while process.poll() is None:
                    self.check();time.sleep(.15)
                output.seek(0,2);size=output.tell();output.seek(max(0,size-12000))
                details=output.read().decode('utf8','replace')
                if process.returncode:raise RuntimeError(f'Process failed ({process.returncode}): {details}')
                return details
            except BaseException:
                if process.poll() is None:
                    if os.name=='nt':
                        subprocess.run([str(Path(os.environ['SystemRoot'])/'System32/taskkill.exe'),'/PID',str(process.pid),'/T','/F'],capture_output=True,
                            creationflags=subprocess.CREATE_NO_WINDOW)
                    else:process.terminate()
                    try:process.wait(timeout=5)
                    except subprocess.TimeoutExpired:process.kill();process.wait()
                raise
