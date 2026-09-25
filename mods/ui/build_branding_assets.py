"""Build original loading artwork only; no manufacturer image pack is shipped."""
from pathlib import Path
import hashlib
import sys
import numpy as np

def build(source: Path, output: Path):
    sys.path.insert(0, str(source / 'vendor'))
    from tools.xz_gui.theme import make_splash, make_unloaded_logo
    output.mkdir(parents=True, exist_ok=True)
    font=source/'mods/ui/fonts/BarlowSemiCondensed-Medium.ttf'
    logo=source/'mods/ui/assets/xz-mods.png'
    for name, image in [('splash.rgb565',make_splash(font,logo)),('logo.rgb565',make_unloaded_logo(font,logo))]:
        rgb=np.asarray(image.convert('RGB'),dtype=np.uint16)
        pixels=((rgb[:,:,0]>>3)<<11)|((rgb[:,:,1]>>2)<<5)|(rgb[:,:,2]>>3)
        (output/name).write_bytes(pixels.astype('<u2').tobytes())
    (output/'apply.sh').write_text('''#!/bin/sh
set -eu
pack="$1"
assets=$(dirname "$0")
[ "$(wc -c < "$pack")" -eq 17666908 ]
[ "$(dd if="$pack" bs=44 count=1582 2>/dev/null | md5sum | cut -d ' ' -f 1)" = 1160eaec6cbb0e6e13ea22232a4b2edf ]
[ "$(wc -c < "$assets/splash.rgb565")" -eq 768000 ]
[ "$(wc -c < "$assets/logo.rgb565")" -eq 103040 ]
dd if="$assets/splash.rgb565" of="$pack" bs=8 seek=1541365 conv=notrunc 2>/dev/null
dd if="$assets/logo.rgb565" of="$pack" bs=8 seek=1779173 conv=notrunc 2>/dev/null
''',encoding='ascii',newline='\n')
    names=['splash.rgb565','logo.rgb565','apply.sh']
    (output/'MD5SUMS').write_text(''.join(hashlib.md5((output/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names),encoding='ascii',newline='\n')

if __name__=='__main__':
    build(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve())
