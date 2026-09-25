# SPDX-License-Identifier: MIT
"""JSON-line backend for the independent XZ Mods desktop application."""
from __future__ import annotations
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import re
from . import cache,usb
from .jobs import Job,Cancelled

def resources():
    configured=os.environ.get('XZ_BUILDER_RESOURCES')
    if not configured:raise ValueError('Builder resources are not configured')
    return Path(configured).resolve()

def status():
    root=resources();runtime=root/'runtime'/'manifest.json'
    manifest=json.loads(runtime.read_text()) if runtime.exists() else None
    catalog_path=Path(__file__).with_name('models.json')
    catalog=json.loads(catalog_path.read_text()) if catalog_path.exists() else {}
    return {'name':'XZ Mods','version':'0.1.4-preview','firmware':'XDJ-XZ 1.26',
        'prepared_formats':['overcue-stems/4','stemd-cache/1'],
        'separation_output_format':'stemd-cache/1',
        'release_ready':False,'runtime_present':manifest is not None,'runtime':manifest,
        'vjtools_required':False,'vjtools_connection':True,'engines':catalog,
        'limits':{'sample_rate':44100,'channels':2,'source_formats':['WAV','FLAC'],
                  'max_stem_pcm_bytes':cache.MAX_PCM_BYTES},
        'release_gates':['Physical pad/audio retest','Native inline UI acceptance',
            'Fresh boot and two-deck qualification','Real model execution and cache timing tests',
            'Packaged dependency/source notices audit']}

def import_stems(request,job):
    volume=cache._safe_path(request['volume'])
    source=cache._regular(request['source'])
    job.progress('inspect','Checking source and aligned stem files')
    info=cache.inspect_audio(source)
    key=cache.track_key(source,info['frames'])
    model=cache.validate_model_id(request['separation_id'])
    job.progress('cache','Writing the compatible stem cache without replacing existing files')
    existing=cache._safe_path(volume/'mods/stemd-cache'/model/key[:2]/key)
    if existing.exists():
        checked=inspect_cache_entry(existing,source)
        for part in ('harmonics','vocals'):
            if usb.sha(cache._regular(request[part]))!=usb.sha(checked[part]) or cache._gain(request.get(part+'_gain',1))!=checked[part+'_gain']:
                raise FileExistsError('This model ID already has different stems. Existing audio has been preserved.')
        job.check();cache.select_model(volume,key,model)
        result={**checked,'directory':str(existing),'reused':True,'sample_rate':44100}
    else:
        result=cache.import_cache(volume,source,request['harmonics'],request['vocals'],model,
            request.get('harmonics_gain',1),request.get('vocals_gain',1))
    try:source.relative_to(volume);target=source
    except ValueError:
        folder=volume/'XZ Mods Music';cache._safe_path(folder);folder.mkdir(exist_ok=True)
        target=folder/(key+'-'+source.name)
        if target.exists():
            if usb.sha(target)!=usb.sha(source):raise FileExistsError('Prepared track path already contains different audio')
        else:
            with tempfile.NamedTemporaryFile(prefix='.track-',dir=folder,delete=False) as stream:
                temporary=Path(stream.name)
            try:
                shutil.copyfile(source,temporary);job.check()
                cache._publish_new(temporary,target)
            finally:
                if temporary.exists():temporary.unlink()
    result['track_on_usb']=str(target)
    result['reload_track_to_apply']=True
    return result

def inspect_cache_entry(entry,source):
    entry=cache._safe_path(entry)
    if not re.fullmatch('[0-9a-f]{16}',entry.name):raise ValueError('Choose the upstream cache entry folder containing meta and the two stems')
    fields={}
    for line in cache._regular(entry/'meta').read_text(encoding='ascii').splitlines():
        if not line.strip():continue
        name,separator,value=line.partition('=')
        if not separator or name in fields:raise ValueError('Cache metadata is malformed')
        fields[name]=value
    if fields.get('v')!='1':raise ValueError('Only upstream cache version 1 is supported')
    frames=int(fields['frames'])
    info=cache.inspect_audio(source)
    if info['frames']!=frames or cache.track_key(source,frames)!=entry.name:
        raise ValueError('This cache does not match the selected original track')
    result={'separation_id':entry.parent.parent.name,'frames':frames,'key':entry.name}
    for name in ('harmonics','vocals'):
        paths=[entry/(name+extension) for extension in ('.wav','.flac') if (entry/(name+extension)).exists()]
        if len(paths)!=1 or cache.inspect_audio(paths[0])['frames']!=frames:raise ValueError('Cache stem is missing, ambiguous or misaligned')
        result[name]=str(paths[0]);result[name+'_gain']=cache._gain(float(fields[name]))
    return result

def dispatch(request,job):
    method=request.get('method')
    if method=='status':return status()
    if method=='inspect_overcue':
        source=cache._regular(request['source'])
        job.progress('verify','Checking the OverCue source identity and every prepared audio page')
        output=job.run([resources()/'xz-overcue-check.exe',source])
        return json.loads(output)
    if method=='download_firmware':
        from .engines import data_root,download
        record={'filename':'XDJXZ_v126.zip','url':'https://downloads.support.alphatheta.com/firmwares/all-in-one-dj-systems/XDJ-XZ/XDJXZ_v126.zip',
            'sha256':'7c4159ca90b0cfd651725a68e10a11cf1a621239f703a0071f24355cc89fed7f','bytes':70775680}
        job.progress('download','Downloading the official XZ 1.26 firmware from AlphaTheta')
        path=download(record,data_root()/'local-firmware',job)
        return {'firmware_path':str(path),'sha256':record['sha256'],'source':'AlphaTheta','local_input_only':True}
    if method=='inspect_audio':return cache.inspect_audio(request['source'])
    if method=='inspect_cache':return inspect_cache_entry(request['entry'],request['source'])
    if method=='inspect_firmware':return usb.inspect_inputs(request['firmware'],request['key'])
    if method=='import_stems':return import_stems(request,job)
    if method=='import_branding':
        from .branding import import_branding
        return import_branding(request,job)
    if method=='build_usb':
        return usb.build_usb(request['volume'],request['firmware'],request['key'],resources(),job,request.get('experimental') is True)
    if method in ('setup_engine','separate'):
        from .engines import setup,separate
        return setup(request,job,resources()) if method=='setup_engine' else separate(request,job,resources())
    raise ValueError('Unknown builder operation')

def main():
    try:
        raw=sys.stdin.buffer.read(65537)
        if len(raw)>65536:raise ValueError('Request is too large')
        request=json.loads(raw)
        if not isinstance(request,dict):raise ValueError('Expected an operation object')
        result=dispatch(request,Job(os.environ.get('XZ_BUILDER_CANCEL_FILE')))
        print(json.dumps({'event':'result','ok':True,'result':result}),flush=True)
    except Cancelled:
        print(json.dumps({'event':'result','ok':False,'cancelled':True,'error':'Cancelled'}),flush=True)
    except Exception as exc:
        # Input secrets are never included in diagnostics or requests echoed to stdout.
        print(json.dumps({'event':'result','ok':False,'error':str(exc)}),flush=True)

if __name__=='__main__':main()
