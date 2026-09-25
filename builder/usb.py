"""Build locally from supplied firmware inputs, then publish a verified new image."""
from __future__ import annotations
import hashlib
import json
import ctypes
import os
from pathlib import Path
import shutil
import tempfile
from .cache import _safe_path,_regular,_publish_new
from . import firmware

def sha(path):
    with Path(path).open('rb') as file:return hashlib.file_digest(file,'sha256').hexdigest()

def target_info(path):
    path=_safe_path(path)
    if not path.is_dir():raise ValueError('Choose an existing USB root or staging folder')
    filesystem=None;root=None
    if os.name=='nt':
        get_root=ctypes.windll.kernel32.GetVolumePathNameW
        get_root.argtypes=[ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_uint32];get_root.restype=ctypes.c_int
        get_info=ctypes.windll.kernel32.GetVolumeInformationW
        word=ctypes.POINTER(ctypes.c_uint32)
        get_info.argtypes=[ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_uint32,word,word,word,ctypes.c_wchar_p,ctypes.c_uint32]
        get_info.restype=ctypes.c_int
        buffer=ctypes.create_unicode_buffer(32768)
        if not get_root(str(path),buffer,len(buffer)):
            raise OSError('Could not identify the target volume')
        root=buffer.value;name=ctypes.create_unicode_buffer(64)
        if not get_info(root,None,0,None,None,None,name,len(name)):
            raise OSError('Could not read the target filesystem')
        filesystem=name.value.upper()
        if path==Path(root) and filesystem not in ('FAT','FAT32'):
            raise ValueError('Choose a FAT/FAT32 USB root, or a staging folder. XZ Mods does not format drives.')
    direct=bool(root and path==Path(root) and filesystem in ('FAT','FAT32'))
    return {'filesystem':filesystem,'direct_usb_root':direct,'requires_copy_to_usb_root':not direct}

def require_usb_root(path):
    path=_safe_path(path)
    target=target_info(path)
    if not target['direct_usb_root']:
        raise ValueError('Choose the root of a FAT/FAT32 USB drive. XZ Mods does not format drives or erase music.')
    if (path/'autoexec.bin').exists():
        raise FileExistsError('This USB already has autoexec.bin. Keep that loader or back it up before preparing a new one.')
    return path

def inspect_inputs(rbp,key):
    application_path=_regular(rbp);secret=_regular(key)
    application,key_verified=firmware.import_application(application_path,firmware.effective_key(secret))
    return {'firmware':'XDJ-XZ 1.26','application_md5':hashlib.md5(application).hexdigest(),
        'key_present':True,'key_verified_against_input':key_verified}

def build_usb(volume,rbp,key,resources,job,experimental=False):
    inspect_inputs(rbp,key)
    if not experimental:raise ValueError('This runtime is experimental; a public release has not been qualified')
    volume=_safe_path(volume)
    target=target_info(volume)
    destination=volume/'autoexec.bin';_safe_path(destination)
    if destination.exists():raise FileExistsError('autoexec.bin already exists. Back it up or choose an empty staging folder; this builder will not overwrite it.')
    resources=Path(resources)
    manifest=json.loads((resources/'runtime'/'manifest.json').read_text())
    runtime=resources/'runtime'/'libxz-mods.so';receiver=resources/'runtime'/'libxz-receiver.so'
    for name,path in [('runtime_sha256',runtime),('receiver_sha256',receiver)]:
        if sha(path)!=manifest[name]:raise ValueError('Bundled mod files failed integrity verification')
    secret=firmware.effective_key(key)
    patched,key_verified=firmware.import_application(rbp,secret)
    temporary=Path(tempfile.mkdtemp(prefix='.xzmods-build-',dir=volume))
    try:
        staging=temporary/'payload'
        job.progress('build','Assembling the standalone mod and VJ.Tools connection')
        expected=firmware.stage_payload(staging,patched,runtime,receiver,resources/'bootstrap.sh')
        branding=resources/'branding'
        if branding.is_dir():
            (staging/'branding').mkdir()
            for name in ('apply.sh','MD5SUMS','splash.rgb565','logo.rgb565'):
                data=(branding/name).read_bytes()
                (staging/'branding'/name).write_bytes(data)
                expected['branding/'+name]=data
        notices=staging/'licenses';notices.mkdir()
        for path in (resources/'licenses').iterdir():
            if path.is_file():shutil.copyfile(path,notices/path.name)
        image=temporary/'autoexec.bin'
        data=firmware.author_image(staging,secret);image.write_bytes(data)
        job.progress('verify','Verifying the encrypted USB image before installation')
        structure=firmware.verify_image(data,secret,expected)
        digest=sha(image);job.check()
        _safe_path(volume);_safe_path(destination)
        _publish_new(image,destination)
        return {'image':str(destination),'sha256':digest,'bytes':destination.stat().st_size,
            'profile':'experimental','hardware_qualified':False,'contains_local_firmware':True,
            'share_image':False,'key_verified_against_input':key_verified,'vjtools_required':False,'vjtools_connection':True,'structure':structure,**target}
    finally:
        if temporary.exists():
            _safe_path(temporary);shutil.rmtree(temporary)
