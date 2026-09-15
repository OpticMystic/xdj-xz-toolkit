"""Verify allowlisted bundle and sourced loader inside temporary host fixtures."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile
from build import build,SHA256

def shellpath(p):
    s=str(p.resolve()).replace('\\','/')
    return '/'+s[0].lower()+s[2:] if len(s)>1 and s[1]==':' else s

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--shell',required=True);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='vjtools-xz-support-') as tmp:
        root=Path(tmp);bundle=root/'bundle';stage=root/'stage';manifest=build(bundle)
        expected=set(manifest['included_files'])|{'manifest.json'}
        actual={x.relative_to(bundle).as_posix() for x in bundle.rglob('*') if x.is_file()}
        assert actual==expected
        for name,digest in manifest['included_files'].items():assert hashlib.sha256((bundle/name).read_bytes()).hexdigest()==digest
        args=[shellpath(bundle),shellpath(stage)]
        env=dict(os.environ);env.pop('LD_PRELOAD',None)
        def run(script):
            r=subprocess.run([a.shell,'--noprofile','--norc','-c','set -eu\n'+script,'test']+args,env=env,capture_output=True,text=True)
            if r.returncode:raise RuntimeError(f'Loader fixture failed: {r.stdout}\n{r.stderr}')
        run('''
. "$1/vjtools-xz-loader.sh"
[ "${LD_PRELOAD-unset}" = unset ]
[ ! -e "$2" ]
if vjtools_xz_stage "$1" "$2"; then exit 10; fi
export VJTOOLS_XZ_TESTING=1
vjtools_xz_stage "$1" "$2"
[ -f "$VJTOOLS_XZ_PRELOAD_PATH" ]
LD_PRELOAD='/other/mod.so:/second/mod.so'
vjtools_xz_preload "$VJTOOLS_XZ_PRELOAD_PATH"
[ "$LD_PRELOAD" = "/other/mod.so:/second/mod.so:$VJTOOLS_XZ_PRELOAD_PATH" ]
vjtools_xz_preload "$VJTOOLS_XZ_PRELOAD_PATH"
[ "$LD_PRELOAD" = "/other/mod.so:/second/mod.so:$VJTOOLS_XZ_PRELOAD_PATH" ]
LD_PRELOAD="/other/mod.so $VJTOOLS_XZ_PRELOAD_PATH"
vjtools_xz_preload "$VJTOOLS_XZ_PRELOAD_PATH"
[ "$LD_PRELOAD" = "/other/mod.so $VJTOOLS_XZ_PRELOAD_PATH" ]
unset LD_PRELOAD
vjtools_xz_enable "$1" "$2"
[ "$LD_PRELOAD" = "$VJTOOLS_XZ_PRELOAD_PATH" ]
vjtools_xz_enable "$1" "$2"
[ "$LD_PRELOAD" = "$VJTOOLS_XZ_PRELOAD_PATH" ]
unset LD_PRELOAD
''')
        staged=stage/SHA256/'libvjtools-xz-receiver.so'
        assert hashlib.sha256(staged.read_bytes()).hexdigest()==SHA256
        staged.chmod(0o644);staged.write_bytes(b'tampered staged fixture')
        run('''
. "$1/vjtools-xz-loader.sh"
export VJTOOLS_XZ_TESTING=1
LD_PRELOAD=/other/mod.so
if vjtools_xz_enable "$1" "$2"; then exit 11; fi
[ "$LD_PRELOAD" = /other/mod.so ]
unset LD_PRELOAD
''')
        (bundle/'lib/libvjtools-xz-receiver.so').write_bytes(b'tampered source fixture')
        run('''
. "$1/vjtools-xz-loader.sh"
export VJTOOLS_XZ_TESTING=1
if vjtools_xz_enable "$1" "$2"; then exit 12; fi
[ "${LD_PRELOAD-unset}" = unset ]
''')
    print('PASS bundle allowlist, SHA/ELF identity, inert sourcing, explicit staging, preserved/idempotent preload and tamper refusal')
if __name__=='__main__':main()
