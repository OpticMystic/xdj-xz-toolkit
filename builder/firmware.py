# SPDX-License-Identifier: MIT
"""XZ 1.26 image authoring from local inputs and documented cryptoloop geometry.

No firmware application, key, or proprietary graphics are included in this module.
The legacy vendored Python implementation is not imported or packaged.
"""
from __future__ import annotations
import hashlib
import io
from pathlib import Path
import struct
import tarfile
import zipfile
import pycdlib
from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes

STOCK_SHA='6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2'
PATCHED_MD5='6a7ccb454e52afa26a73f3380706c9ca'
PATCHES=((0x132d0c,'440050e30100a083','0300a0e31eff2fe1'),(0xd1fb8,'800080e0ac3c07e3','0100a0e31eff2fe1'))

def effective_key(path):
    raw=Path(path).read_bytes()
    if not raw or len(raw)>4096:raise ValueError('Select a nonempty boot key file')
    line=raw.splitlines()[0]
    if not line:raise ValueError('Boot key is empty')
    return line[:31].ljust(32,b'\0')

def crypt(data,key,decrypt=False):
    if len(data)%512:raise ValueError('Encrypted images require complete 512-byte sectors')
    result=bytearray(len(data))
    for start in range(0,len(data),512):
        cipher=Cipher(algorithms.AES(key),modes.CBC(struct.pack('<I',start//512)+bytes(12)))
        transform=cipher.decryptor() if decrypt else cipher.encryptor()
        result[start:start+512]=transform.update(data[start:start+512])+transform.finalize()
    return bytes(result)

def patched_application(data):
    if hashlib.md5(data).hexdigest()==PATCHED_MD5:return data
    if hashlib.sha256(data).hexdigest()!=STOCK_SHA:raise ValueError('Firmware application is not the supported XDJ-XZ 1.26 version')
    result=bytearray(data)
    for offset,before,after in PATCHES:
        expected=bytes.fromhex(before)
        if result[offset:offset+len(expected)]!=expected:raise ValueError('Firmware patch precondition failed')
        result[offset:offset+len(expected)]=bytes.fromhex(after)
    if hashlib.md5(result).hexdigest()!=PATCHED_MD5:raise ValueError('Patched firmware identity mismatch')
    return bytes(result)

def _open(data):
    if len(data)<34816 or data[32769:32774]!=b'CD001':raise ValueError('Image did not decrypt to ISO9660; check the boot key and input file')
    iso=pycdlib.PyCdlib();iso.open_fp(io.BytesIO(data));return iso

def _member(iso,path):
    output=io.BytesIO();iso.get_file_from_iso_fp(output,rr_path=path);return output.getvalue()

def import_application(path,key):
    path=Path(path)
    if path.stat().st_size>512*1024*1024:raise ValueError('Firmware input exceeds the supported size')
    data=path.read_bytes()
    if data.startswith(b'PK\x03\x04'):
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            updates=[item for item in archive.infolist() if not item.is_dir() and item.filename.lower().endswith('.upd')]
            if len(updates)!=1 or updates[0].file_size>512*1024*1024:raise ValueError('Choose an official ZIP containing one supported updater')
            data=archive.read(updates[0])
    if hashlib.sha256(data).hexdigest()==STOCK_SHA or hashlib.md5(data).hexdigest()==PATCHED_MD5:
        return patched_application(data),False
    data=crypt(data[:len(data)//512*512],key,True)
    iso=_open(data)
    try:
        candidates=[]
        for directory,_,files in iso.walk(rr_path='/'):
            for name in files:
                if name in ('rbp','rbp.patched','pdj','pdj.tar.gz','pdj.tgz'):
                    candidates.append(directory.rstrip('/')+'/'+name)
        for name in candidates:
            content=_member(iso,name)
            if name.rsplit('/',1)[-1] in ('rbp','rbp.patched'):
                return patched_application(content),True
            try:
                with tarfile.open(fileobj=io.BytesIO(content),mode='r:*') as archive:
                    for member in archive:
                        if member.isfile() and member.name.lstrip('./') in ('pdj/rbp','rbp') and member.size<=16*1024*1024:
                            stream=archive.extractfile(member)
                            if stream:return patched_application(stream.read()),True
            except tarfile.TarError:continue
        raise ValueError('No supported XZ 1.26 application found in this image. Official updater variants are not yet qualified.')
    finally:iso.close()

def author_image(tree,key):
    tree=Path(tree);iso=pycdlib.PyCdlib();iso.new(interchange_level=3,vol_ident='UsbAuto',rock_ridge='1.09')
    directories={Path():''};number=0
    try:
        for path in sorted(tree.rglob('*')):
            if not path.is_dir():continue
            relative=path.relative_to(tree);number+=1
            location=directories[relative.parent]+f'/D{number:07d}'
            iso.add_directory(iso_path=location,rr_name=relative.name,file_mode=0o40555)
            directories[relative]=location
        for path in sorted(tree.rglob('*')):
            if not path.is_file():continue
            relative=path.relative_to(tree);number+=1
            iso.add_file(str(path),iso_path=directories[relative.parent]+f'/F{number:07d};1',
                rr_name=relative.name,file_mode=0o100755 if path.suffix in ('.sh','.so') else 0o100644)
        output=io.BytesIO();iso.write_fp(output)
    finally:iso.close()
    data=bytearray(output.getvalue());data.extend(bytes(150*2048))
    blocks=len(data)//2048
    struct.pack_into('<I',data,32768+80,blocks);struct.pack_into('>I',data,32768+84,blocks)
    return crypt(bytes(data),key)

def verify_image(data,key,expected):
    plain=crypt(data,key,True);iso=_open(plain)
    try:
        for name,contents in expected.items():
            if _member(iso,'/'+name)!=contents:raise ValueError(f'Image verification failed for {name}')
    finally:iso.close()
    return {'bytes':len(data),'verified_files':len(expected),'volume':'UsbAuto'}

def stage_payload(tree,application,runtime,receiver,bootstrap):
    tree=Path(tree);(tree/'tools').mkdir(parents=True)
    files={'autoexec.sh':Path(bootstrap).read_bytes().replace(b'\r\n',b'\n'),
        'rbp.patched':application,'tools/libxz-mods.so':Path(runtime).read_bytes(),
        'tools/libxz-directfb-hook.so':Path(receiver).read_bytes(),'mods-mode':b'experimental\n'}
    if not files['autoexec.sh'].startswith(b'#!/bin/sh\n'):raise ValueError('Loader has no POSIX shebang')
    for name in ('rbp.patched','tools/libxz-mods.so','tools/libxz-directfb-hook.so'):
        files[name+'.md5']=(hashlib.md5(files[name]).hexdigest()+'  '+Path(name).name+'\n').encode('ascii')
    for name,data in files.items():(tree/name).write_bytes(data)
    return files
