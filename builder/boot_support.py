"""Resolve XZ boot support automatically from the manufacturer's public source."""
from contextlib import ExitStack
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import tarfile
import tempfile
import zipfile
import zlib
from inflate64 import Inflater
from .cache import _safe_path
from .engines import data_root, download
from .jobs import exclusive_lock

SOURCE_PAGE='https://www.pioneerdj.com/en/support/open-source-code-distribution/gnu-open-source-license/'
PARTS=[
    {'filename':'pioneerdj_xdj_xz.tar.bz2.00.zip','member':'pioneerdj_xdj_xz.tar.bz2.00',
     'url':'https://files.microcms-assets.io/assets/3b9e29ce734e49babfedb3f8d1e728e3/c111011dece644809c0b8fb98cd48bc3/CDBEF6FF-4AEB-4CC2-93FE-16B8D8EC9132.zip',
     'bytes':209448413,'sha256':'81b52d25a1df0890cd83641eae3d705a1cfcea801fc7f9918b7a40bd39e9ee36'},
    {'filename':'pioneerdj_xdj_xz.tar.bz2.01.zip','member':'pioneerdj_xdj_xz.tar.bz2.01',
     'url':'https://files.microcms-assets.io/assets/3b9e29ce734e49babfedb3f8d1e728e3/3936f89830bb4eebaf486b2d4a632112/B42B6EDE-945B-418A-A9DF-E954C8D501ED.zip',
     'bytes':52468250,'sha256':'9c7094b0c958a5928141bd091b150e01b66bbde49a40a275588c6f9a47578eaf'},
]
KEY_SHA256='912e9c3f3090378090e88ac026abda797cbf941492d2b469a05b1e3a57fcc6c5'
INITRAMFS='pioneerdj_xdj_xz/initramfs.tar.gz'
KEY_MEMBER='initramfs/usr/local/pdj/aes256.key'

class _Parts:
    def __init__(self,streams,job):self.streams=iter(streams);self.current=next(self.streams,None);self.job=job
    def read(self,size):
        self.job.check()
        if not 0<=size<=1024*1024:raise ValueError('Unexpected source archive read size')
        chunks=[];left=size
        while left and self.current is not None:
            data=self.current.read(left)
            if data:chunks.append(data);left-=len(data)
            else:self.current=next(self.streams,None)
        return b''.join(chunks)

class _Deflate64Member:
    """Read one pinned Deflate64 ZIP member without unpacking a large source tree."""
    def __init__(self,archive,entry):
        self.file=open(archive.filename,'rb')
        self.file.seek(entry.header_offset)
        header=self.file.read(30)
        if len(header)!=30 or header[:4]!=b'PK\x03\x04':raise ValueError('Invalid XZ source ZIP header')
        filename_size,extra_size=struct.unpack_from('<HH',header,26)
        self.file.seek(entry.header_offset+30+filename_size+extra_size)
        self.remaining=entry.compress_size
        self.expected=entry.file_size
        self.crc_expected=entry.CRC
        self.inflater=Inflater()
        self.buffer=bytearray()
        self.total=0
        self.crc=0
        self.verified=False

    def read(self,size):
        if not 0<=size<=1024*1024:raise ValueError('Unexpected source archive read size')
        while len(self.buffer)<size and self.remaining:
            block=self.file.read(min(self.remaining,1024*1024))
            if not block:raise ValueError('XZ source ZIP member is truncated')
            self.remaining-=len(block)
            decoded=self.inflater.inflate(block)
            self.total+=len(decoded)
            if self.total>self.expected:raise ValueError('XZ source ZIP member exceeds its declared size')
            self.crc=zlib.crc32(decoded,self.crc)
            self.buffer.extend(decoded)
        if not self.remaining and not self.verified:
            if not self.inflater.eof or self.total!=self.expected or self.crc!=self.crc_expected:
                raise ValueError('XZ source ZIP member failed decompression or CRC verification')
            self.verified=True
        result=bytes(self.buffer[:size]);del self.buffer[:size]
        return result

    def close(self):self.file.close()

def _extract(paths,job):
    with ExitStack() as stack:
        streams=[]
        for path,record in zip(paths,PARTS):
            archive=stack.enter_context(zipfile.ZipFile(path))
            if archive.namelist()!=[record['member']]:raise ValueError('Unexpected XZ source archive layout')
            entry=archive.getinfo(record['member'])
            if entry.file_size>210*1024*1024:raise ValueError('XZ source archive part exceeds its limit')
            if entry.compress_type!=9:raise ValueError('Unexpected XZ source ZIP compression')
            member=_Deflate64Member(archive,entry)
            stack.callback(member.close)
            streams.append(member)
        with tarfile.open(fileobj=_Parts(streams,job),mode='r|bz2') as outer:
            for member in outer:
                job.check()
                if member.offset_data+member.size>1024*1024*1024:raise ValueError('XZ source archive exceeds its limit')
                if member.name.lstrip('./')!=INITRAMFS:continue
                if not member.isfile() or not 0<member.size<=8*1024*1024:raise ValueError('Unexpected XZ support archive')
                with outer.extractfile(member) as source:
                    nested=source.read(member.size+1)
                with tarfile.open(fileobj=io.BytesIO(nested),mode='r:gz') as inner:
                    for entry in inner:
                        job.check()
                        if entry.name.lstrip('./')!=KEY_MEMBER:continue
                        if not entry.isfile() or not 0<entry.size<=4096:raise ValueError('Unexpected XZ support file')
                        with inner.extractfile(entry) as source:result=source.read(4097)
                        if hashlib.sha256(result).hexdigest()!=KEY_SHA256:raise ValueError('XZ support file integrity check failed')
                        return result
                break
    raise ValueError('Required XZ support file was not found in the published source')

def ensure_boot_key(job):
    root=_safe_path(data_root()/'boot-support');root.mkdir(exist_ok=True)
    target=_safe_path(root/'aes256.key')
    with exclusive_lock(_safe_path(root/'.setup.lock')):
        job.check()
        if target.is_file() and hashlib.sha256(target.read_bytes()).hexdigest()==KEY_SHA256:return target
        job.progress('setup','Preparing required XZ support files automatically (first setup: about 250 MB)')
        archives=[download(record,root/'downloads',job) for record in PARTS]
        job.progress('setup','Verifying and preparing XZ boot support')
        content=_extract(archives,job)
        with tempfile.NamedTemporaryFile(prefix='.support-',dir=root,delete=False) as file:
            temporary=Path(file.name);file.write(content);file.flush();os.fsync(file.fileno())
        try:
            job.check();_safe_path(target);os.replace(temporary,target)
        finally:
            if temporary.exists():temporary.unlink()
        (root/'provenance.json').write_text(json.dumps({'source':SOURCE_PAGE,'archives':PARTS,'key_sha256':KEY_SHA256},indent=2)+'\n',encoding='utf8')
        return target
