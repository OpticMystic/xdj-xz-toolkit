"""Build a local, allowlisted VJ.Tools receiver support bundle for other XZ mods."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[3]
BINARY=REPO/'packages/xdj-xz-toolkit/vendor/build/libxz-directfb-hook-v49-exclusive-abi14.so'
SHA256='cf381be6d68f64455713524bf94e20d97235e7c93240bb20f7daf0bd8111c0ea'
SOURCE_COMMIT='c0cac3d58c9f15a08fe5341aa4bd6d235ee558f4'
SOURCE_PATH='packages/xdj-xz-toolkit/vendor/tools/xz_runtime/xz_directfb_hook.c'
SOURCE_SHA256='ebcf12aab2aea42f55a3128bd0f0d12cff1c0dadada3964b1c7b1ff70e580e17'

def digest(b):return hashlib.sha256(b).hexdigest()
def inspect_binary(path):
    data=path.read_bytes()
    if digest(data)!=SHA256:raise ValueError('Receiver binary differs from pinned v49 artifact')
    with path.open('rb') as f:
        e=ELFFile(f)
        if e.elfclass!=32 or not e.little_endian or e['e_machine']!='EM_ARM' or e['e_type']!='ET_DYN':raise ValueError('Expected ARM32 little-endian shared library')
        libs=[t.needed for t in e.get_section_by_name('.dynamic').iter_tags() if t.entry.d_tag=='DT_NEEDED']
        if set(libs)!={'libdl.so.2','libpthread.so.0','libc.so.6'}:raise ValueError('Unexpected receiver dependencies')
        return {'elf_class':32,'machine':'ARM','type':'ET_DYN','flags':e['e_flags'],'abi':'existing ARM hard-float ELF flag, preserved byte-for-byte','needed':libs,'size':len(data),'sha256':digest(data)}

def build(output):
    info=inspect_binary(BINARY)
    source=(ROOT/'pinned/xz_directfb_hook-v49.c').read_bytes()
    if digest(source)!=SOURCE_SHA256:raise ValueError('Receiver source snapshot changed')
    if not source.startswith(b'// SPDX-License-Identifier: MPL-2.0'):raise ValueError('Receiver source license changed')
    output=output.resolve()
    if output.exists() and any(output.iterdir()):raise ValueError('Output must be a new or empty directory')
    files={
      'lib/libvjtools-xz-receiver.so':BINARY.read_bytes(),
      'source/xz_directfb_hook.c':source,
      'vjtools-xz-loader.sh':(ROOT/'loader.sh.in').read_text().replace('@SHA256@',SHA256).replace('@MD5@',hashlib.md5(BINARY.read_bytes()).hexdigest()).encode(),
      'README.md':(ROOT/'README.md').read_bytes(),
      'LICENSE-MIT':(REPO/'LICENSE').read_bytes(),
      'NOTICE-MPL-2.0.txt':(ROOT/'NOTICE-MPL-2.0.txt').read_bytes(),
    }
    manifest={'schema_version':1,'name':'vjtools-xz-support','version':'1.0.0','receiver_version':'v49-exclusive-abi14',
      'target':'XDJ-XZ firmware 1.26, existing DirectFB receiver ABI','standalone_audio_features':False,
      'explicit_loader_call_required':True,'binary':info,
      'device_integrity':{'algorithm':'MD5','digest':hashlib.md5(BINARY.read_bytes()).hexdigest(),'utility':'md5sum'},
      'protocol':{'tcp_port':50005,'udp_discovery_port':50006,'discovery_request':'VJDISCOVER','discovery_response':'VJXZ 1 <ip> 50005','messages':['VJFS','VJVP','VJFA','VJFT','VJTE'],'protocol_changed':False},
      'source':{'repo_path':SOURCE_PATH,'commit':SOURCE_COMMIT,'sha256':digest(source),'license':'MPL-2.0','reproducible_build_verified':False},
      'included_files':{k:digest(v) for k,v in files.items()},
      'excluded':['firmware','rbp','keys','firmware archives','catalogs','user media','device credentials'],
      'distribution':{'published':False,'receiver_bytes_modified':False}}
    files['manifest.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    for name,data in files.items():
        path=output/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    return manifest

def archive(bundle, destination):
    manifest=json.loads((bundle/'manifest.json').read_text())
    names=set(manifest['included_files'])|{'manifest.json'}
    if {p.relative_to(bundle).as_posix() for p in bundle.rglob('*') if p.is_file()}!=names:
        raise ValueError('Unexpected files in support bundle')
    for name,expected in manifest['included_files'].items():
        if digest((bundle/name).read_bytes())!=expected:raise ValueError('Bundle hash mismatch: '+name)
    with zipfile.ZipFile(destination,'x',compression=zipfile.ZIP_DEFLATED) as output:
        for name in sorted(names):
            info=zipfile.ZipInfo('vjtools-xz-support/'+name,date_time=(1980,1,1,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED
            output.writestr(info,(bundle/name).read_bytes())
    return digest(destination.read_bytes())
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--archive',type=Path)
    args=parser.parse_args();result=build(args.output)
    zipped=archive(args.output,args.archive) if args.archive else None
    print(json.dumps({'output':str(args.output),'sha256':result['binary']['sha256'],'files':len(result['included_files'])+1,'archive_sha256':zipped}))
