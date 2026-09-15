"""Read the bounded pad diagnostic ring without changing device memory."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import struct
from elftools.elf.elffile import ELFFile
from device_smoke import command

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('receipt',type=Path)
parser.add_argument('binary',type=Path)
parser.add_argument('output',type=Path)
parser.add_argument('--host',default='169.254.168.59')
args=parser.parse_args()
if args.output.exists():raise ValueError('Choose a new evidence file')
receipt=json.loads(args.receipt.read_text())
if hashlib.sha256(args.binary.read_bytes()).hexdigest()!=receipt['runtime_sha256']:
    raise ValueError('Binary differs from deployed receipt')
with args.binary.open('rb') as f:
    symbols=ELFFile(f).get_section_by_name('.dynsym').get_symbol_by_name('xz_mods_pad_trace_v1')
    if not symbols or symbols[0]['st_size']!=16+64*40:raise ValueError('Unexpected trace ABI')
    offset=int(symbols[0]['st_value'])
pid=command(args.host,'pidof rbp').strip()
if not re.fullmatch('[0-9]+',pid):raise ValueError('Expected one application process')
path=receipt['remote']+'/mods.so'
maps=command(args.host,f'cat /proc/{pid}/maps').splitlines()
mapping=[line.split() for line in maps if line.split()[-1]==path and line.split()[2]=='00000000']
if len(mapping)!=1:raise ValueError('Deployed library is not mapped as expected')
address=int(mapping[0][0].split('-')[0],16)+offset
def read(address,length):
    start=address//4096; count=(address%4096+length+4095)//4096
    blob=base64.b64decode(''.join(command(args.host,f'dd if=/proc/{pid}/mem bs=4096 skip={start} count={count} 2>/dev/null | base64').splitlines()),validate=True)
    if len(blob)!=count*4096:raise ValueError('Incomplete trace memory read')
    return blob[address%4096:address%4096+length]
for attempt in range(5):
    blob=read(address,16+64*40)
    version,sequence,count,enabled=struct.unpack_from('<4I',blob)
    after=struct.unpack('<I',read(address+4,4))[0]
    if sequence==after and not sequence%2:break
else:raise RuntimeError('Trace is changing; retry after releasing the pads')
if version!=1:raise ValueError('Unknown trace format')
names=['key','operation_byte','channel','mode','decoded','deck','pad','operation','hotcue','ui_flags']
flags=['started','visible','stems_page','selected_deck','stems_enabled','audio_available','hotcue_mode','consumed','toggled',
       'bypass','drums_muted','harmonics_muted','vocals_muted','last_render_cache_ready']
records=[]
for i in range(max(0,count-64),count):
    row=dict(zip(names,struct.unpack_from('<10I',blob,16+(i%64)*40)))
    row['key']=hex(row['key'])
    row['gates']=[name for bit,name in enumerate(flags) if row['ui_flags']&(1<<bit)]
    row['ordinal']=i
    records.append(row)
result={'pid':int(pid),'runtime_sha256':receipt['runtime_sha256'],'enabled':bool(enabled),'count':count,'records':records}
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
