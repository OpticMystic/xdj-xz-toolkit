"""Offline native skin candidate. Does not deploy or qualify a whole-interface skin."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'vendor'))
from tools.xz_gui.image_pack import ImagePack

def build(source,table):
    pack=ImagePack(source)
    if len(pack.entries)!=1582:raise ValueError('Expected XZ 1.26 GUI inventory')
    lut=np.frombuffer(table,dtype='<u2')
    if len(lut)!=65536:raise ValueError('Expected complete RGB565 mapping')
    output=bytearray(source);changed=[];keys=0
    for entry in pack.entries:
        start=entry.data_offset;end=start+entry.pixel_size
        pixels=np.frombuffer(source[start:end],dtype='<u2')
        mapped=lut[pixels].copy()
        key=pixels==0xf81f
        mapped[key]=pixels[key]
        keys+=int(key.sum())
        encoded=mapped.tobytes()
        if encoded!=source[start:end]:changed.append(entry.index)
        output[start:end]=encoded
        assert output[end:end+entry.padding_size]==source[end:end+entry.padding_size]
    result=ImagePack(bytes(output))
    if result.entries!=pack.entries:raise ValueError('Asset layout changed')
    return bytes(output),{'changed_images':changed,'preserved_magenta_key_pixels':keys,
                         'asset_count':len(pack.entries),'hardware_qualified':False,
                         'scope':'static artwork palette candidate; native text, drawing and redraw remain separate'}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,required=True)
    parser.add_argument('--lut',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists() or args.evidence.exists():parser.error('Choose new output and evidence paths')
    source=args.input.read_bytes();result,report=build(source,args.lut.read_bytes())
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(result)
    report.update(input_sha256=hashlib.sha256(source).hexdigest(),output_sha256=hashlib.sha256(result).hexdigest())
    args.evidence.parent.mkdir(parents=True,exist_ok=True);args.evidence.write_text(json.dumps(report,indent=2)+'\n')
    pack=ImagePack(result);page=Image.new('RGB',(800,350),(181,201,139));draw=ImageDraw.Draw(page)
    for n,index in enumerate((77,78,81,82,113,117,125,126,1446,1487)):
        asset=pack.image(index);asset.thumbnail((150,135))
        x=(n%5)*160;y=(n//5)*175;page.paste(asset,(x,y+20));draw.text((x+4,y+4),str(index),fill=(29,43,32))
    page.save(args.evidence.with_suffix('.png'))
    print(json.dumps({k:v for k,v in report.items() if k!='changed_images'}))

if __name__=='__main__':main()
