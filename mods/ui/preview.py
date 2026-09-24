"""Render native C UI previews locally. Never connects to the XZ or VJ.Tools."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from PIL import Image

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--zig',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--reference',type=Path)
a=p.parse_args();root=Path(__file__).resolve().parent
a.output.mkdir(parents=True,exist_ok=False)
with tempfile.TemporaryDirectory(prefix='xz-ui-preview-') as temporary:
    exe=Path(temporary)/'preview.exe'
    subprocess.run([str(Path(a.zig).resolve()),'cc','-O2','-std=c11','-Wall','-Wextra','-Werror',
        str(root/'ui.c'),str(root/'preview.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(a.output.resolve())],check=True)
for path in a.output.glob('*.ppm'):
    Image.open(path).save(path.with_suffix('.png'));path.unlink()
reference=''
if a.reference:
    shutil.copyfile(a.reference,a.output/'native-reference.png')
    reference='<details><summary>Captured native XZ screen</summary><img src="native-reference.png" width="800" height="480" alt="Previously captured native XZ screen"></details>'
(a.output/'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>XZ native-style UI preview</title>
<style>body{background:#11151a;color:#edf0f4;font:16px system-ui;margin:24px auto;max-width:840px;padding:0 16px}h1{font-size:24px}p{color:#b6c0cb;line-height:1.5}nav{display:flex;gap:8px;flex-wrap:wrap;margin:18px 0}button,select{background:#26313c;color:white;border:1px solid #7890a5;padding:9px 12px;font:inherit;cursor:pointer}button[aria-pressed=true]{border-color:#ffb13e;background:#513a16}img{display:block;max-width:100%;height:auto;border:1px solid #465362;margin:16px 0}summary{cursor:pointer;margin:20px 0}small{color:#b6c0cb}</style>
<h1>XZ native-style UI</h1><p>Local previews from the actual C renderer. Example track and connection data. These changes have not been loaded on the XZ.</p>
<label>Theme <select id="theme"><option value="0">Original / native</option><option value="1">White</option><option value="2">Cyberpunk</option><option value="3">Neon</option><option value="4">Mocha</option><option value="5">Aurora</option><option value="6">Sandstone</option></select></label>
<nav id="pages"></nav><img id="screen" src="theme-0-stems.png" width="800" height="480" alt="Stems page preview"><small id="caption"></small>
<h2>Two-deck stem rows</h2><p>Two 40-pixel rows for Deck 1 and Deck 2. Each has Vocals, Harmonics, Drums and Bypass. Native placement and touch still need physical acceptance.</p><img id="inline" src="theme-0-inline.png" width="536" height="80" alt="Two stem control rows, one for each deck">
<details><summary>Unavailable and external-deck states</summary><img src="unavailable.png" width="800" height="480" alt="Unavailable stem controls"><img src="external.png" width="800" height="480" alt="External USB deck controls"></details>
'''+reference+'''
<script>const pages=[['stems','Stems / GC'],['muted','Drums muted'],['xpad','X-PAD'],['settings','Settings'],['controls','Controls'],['themes','Themes'],['connection','VJ.Tools']];let page='stems';const theme=document.getElementById('theme');function update(){const image=document.getElementById('screen');image.src=`theme-${theme.value}-${page}.png`;image.alt=pages.find(p=>p[0]===page)[1]+' preview';document.getElementById('inline').src=`theme-${theme.value}-inline.png`;document.getElementById('caption').textContent='800 × 480 pixels. All features remain visible; unavailable controls are marked NOT READY.';document.querySelectorAll('nav button').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.page===page)));}for(const [key,label] of pages){const button=document.createElement('button');button.textContent=label;button.dataset.page=key;button.onclick=()=>{page=key;update()};document.getElementById('pages').append(button);}theme.onchange=update;update();</script></html>''',encoding='utf8')
manifest={'device_access':False,'installed':False,'source_sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ['ui.c','ui.h','font_atlas.h','preview.c']},'images':{path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(a.output.glob('*.png'))}}
(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.output/'index.html')
