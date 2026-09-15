# SPDX-License-Identifier: MIT
"""Managed, independent CPU separation setup with verified model downloads."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import urllib.request
from . import cache
from .jobs import exclusive_lock

def registry():return json.loads(Path(__file__).with_name('models.json').read_text())

def data_root():
    configured=os.environ.get('XZ_BUILDER_DATA')
    base=Path(configured) if configured else Path(os.environ.get('LOCALAPPDATA',Path.home()/'.local/share'))/'XZ Mods'
    base=cache._safe_path(base);base.mkdir(parents=True,exist_ok=True);return base

def file_sha(path):
    with Path(path).open('rb') as file:return hashlib.file_digest(file,'sha256').hexdigest()

def download(record,folder,job):
    name=record['filename']
    if Path(name).name!=name or not record['url'].startswith('https://'):raise ValueError('Invalid packaged artifact manifest')
    folder=cache._safe_path(folder);folder.mkdir(parents=True,exist_ok=True)
    target=cache._safe_path(folder/name)
    if target.is_file() and target.stat().st_size==record['bytes'] and file_sha(target)==record['sha256']:return target
    with tempfile.NamedTemporaryFile(prefix='.download-',dir=folder,delete=False) as file:temporary=Path(file.name)
    try:
        request=urllib.request.Request(record['url'],headers={'User-Agent':'XZMods/0.1'})
        digest=hashlib.sha256();received=0;last_report=0
        with urllib.request.urlopen(request,timeout=15) as response,temporary.open('wb') as output:
            if not response.geturl().startswith('https://'):raise ValueError('Artifact download redirected away from HTTPS')
            while True:
                job.check();block=response.read(1024*1024)
                if not block:break
                received+=len(block)
                if received>record['bytes']:raise ValueError('Artifact exceeds its declared size')
                output.write(block);digest.update(block)
                if received-last_report>=8*1024*1024:
                    job.progress('download',f'{name}: {received*100//record["bytes"]}%');last_report=received
        if received!=record['bytes'] or digest.hexdigest()!=record['sha256']:raise ValueError(f'Integrity check failed for {name}')
        cache._safe_path(target);os.replace(temporary,target)
        return target
    finally:
        if temporary.exists():temporary.unlink()

def setup(request,job,resources):
    with exclusive_lock(cache._safe_path(data_root()/'.engine.lock')):return _setup(request,job,resources)

def _setup(request,job,resources):
    catalog=registry();preset=request['preset']
    if preset not in catalog['presets']:raise ValueError('Unknown separation preset')
    base=data_root();environment=base/'engine-python';python=environment/('Scripts/python.exe' if os.name=='nt' else 'bin/python')
    uv=Path(resources)/'uv.exe'
    if not uv.is_file():raise ValueError('The packaged engine installer is missing')
    marker=base/('setup-'+preset+'.json')
    identity=hashlib.sha256(json.dumps(catalog,sort_keys=True).encode()).hexdigest()
    ready=marker.is_file() and json.loads(marker.read_text()).get('registry_sha256')==identity and python.is_file()
    if not ready:
        job.progress('runtime','Setting up a separate Python runtime for XZ Mods')
        if not python.is_file():job.run([uv,'venv','--python','3.11',environment])
        job.progress('runtime','Installing the CPU separation runtime; this does not use VJ.Tools')
        job.run([uv,'pip','install','--python',python,'--index-url','https://download.pytorch.org/whl/cpu','torch==2.8.0','torchaudio==2.8.0'])
        packages=['openunmix==1.3.0','numpy==2.2.6','scipy==1.16.1']
        if preset=='vocal-focus':packages.extend(name+'=='+version for name,version in catalog['vocal_focus_dependencies'].items())
        job.run([uv,'pip','install','--python',python,*packages])
    models=base/'models'
    for name in catalog['presets'][preset]['models']:
        job.progress('models','Checking '+name);download(catalog['models'][name],models,job)
    code=base/'smule-code'
    if preset=='vocal-focus':
        for record in catalog['smule_code']['files'].values():download(record,code,job)
    notices=base/'licenses';notices.mkdir(exist_ok=True)
    for name,license in catalog['licenses'].items():(notices/(name+'.txt')).write_text(license['text'])
    lock=job.run([uv,'pip','freeze','--python',python])
    (base/'installed-runtime.txt').write_text(lock)
    result={'preset':preset,'registry_sha256':identity,'python':str(python),'models':str(models),'smule_code':str(code),
        'code_and_weights_license':'MIT','runtime_validation_verified':False,'installed':True}
    marker.write_text(json.dumps(result,indent=2)+'\n')
    return result

def separate(request,job,resources):
    with exclusive_lock(cache._safe_path(data_root()/'.engine.lock')):return _separate(request,job,resources)

def _separate(request,job,resources):
    source=cache._regular(request['source']);info=cache.inspect_audio(source)
    if info['frames']*8>cache.MAX_PCM_BYTES:raise ValueError('Track exceeds the current XZ decoded stem memory limit')
    state=_setup(request,job,resources)
    with tempfile.TemporaryDirectory(prefix='xz-separate-',dir=data_root()) as temporary:
        temporary=Path(temporary);canonical=source
        if source.suffix.lower()=='.flac':
            canonical=temporary/'source.wav';job.run([Path(resources)/'xz-audio-helper.exe','convert',source,canonical])
        output=temporary/'separated'
        adapter=Path(resources)/'inference'/'inference.py'
        argv=[state['python'],'-I',adapter,'--preset',request['preset'],'--input',canonical,
              '--model-dir',state['models'],'--output-dir',output,'--device','cpu']
        if request['preset']=='vocal-focus':argv+=['--smule-code',state['smule_code']]
        env=os.environ.copy();env.update({'OMP_NUM_THREADS':'1','MKL_NUM_THREADS':'1','CUDA_VISIBLE_DEVICES':'-1'})
        job.progress('separate','Separating audio on CPU. Your original track is unchanged.')
        job.run(argv,env=env)
        result=json.loads((output/'result.json').read_text())
        stems={item['name']:item for item in result['stems']}
        for item in stems.values():
            if file_sha(output/item['file'])!=item['sha256']:raise ValueError('Generated stem integrity check failed')
        from .service import import_stems
        exported=import_stems({'volume':request['volume'],'source':str(source),'separation_id':result['model_id'],
            'harmonics':str(output/stems['harmonics']['file']),'vocals':str(output/stems['vocals']['file']),
            'harmonics_gain':stems['harmonics']['gain'],'vocals_gain':stems['vocals']['gain']},job)
        return {**exported,'preset':request['preset'],'alignment_verified':False,'vjtools_required':False}
