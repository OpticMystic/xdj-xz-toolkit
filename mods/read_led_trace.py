"""Read bounded LED counters from the exact deployed private runtime."""
import argparse,base64,hashlib,json,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from device_smoke import command
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('receipt',type=Path);p.add_argument('binary',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();receipt=json.loads(a.receipt.read_text())
if a.output.exists():raise ValueError('Use a new evidence path')
if hashlib.sha256(a.binary.read_bytes()).hexdigest()!=receipt['runtime_sha256']:raise ValueError('Binary mismatch')
with a.binary.open('rb') as stream:
 symbol=ELFFile(stream).get_section_by_name('.dynsym').get_symbol_by_name('xz_mods_led_trace_v1')[0]
 offset=int(symbol['st_value']);size=int(symbol['st_size'])
if size!=48:raise ValueError('Unknown LED trace ABI')
host='169.254.168.59';pid=command(host,'pidof rbp').strip()
if not pid.isdecimal():raise ValueError('Expected one running player')
path=receipt['remote']+'/mods.so'
maps=[line.split() for line in command(host,f'cat /proc/{pid}/maps').splitlines()]
base=[int(row[0].split('-')[0],16) for row in maps if row[-1]==path and row[2]=='00000000']
if len(base)!=1:raise ValueError('Expected deployed runtime mapping')
address=base[0]+offset;page=address//4096;count=(address%4096+size+4095)//4096
blob=base64.b64decode(''.join(command(host,f'dd if=/proc/{pid}/mem bs=4096 skip={page} count={count} 2>/dev/null | base64').splitlines()),validate=True)
values=struct.unpack_from('<12I',blob,address%4096)
names=['version','calls','invalid_player_channel','invalid_innards','innards_channel_mismatch','last_record_count','last_native_mode','pad_records','matching_channel_records','forced_records','changed_records','reserved']
result={'pid':pid,'counters':dict(zip(names,values)),'snapshot':'Independent atomic counters; not one transaction'}
a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
