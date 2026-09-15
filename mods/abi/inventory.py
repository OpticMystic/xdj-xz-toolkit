#!/usr/bin/env python3
"""Extract a reproducible read-only native ABI inventory; never patch firmware."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile

EXPECTED_SHA256 = "6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2"
TERMS = ("HotCue", "Grid", "Playlist", "PlayList", "PlayerInnards", "IKeyInput", "Needle", "DjEngineIF", "AudioIODevice", "AudioSourcePlayer", "DirectFB")

def inventory(path):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError("Unknown rbp SHA-256; refusing firmware-specific ABI assertions")
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_ARM' or elf['e_type'] != 'ET_EXEC':
            raise ValueError("Expected ELF32 little-endian ARM ET_EXEC")
        dyn = elf.get_section_by_name('.dynsym')
        exported = {s.name for s in dyn.iter_symbols()}
        syms = list(elf.get_section_by_name('.symtab').iter_symbols())
        functions = {s['st_value']: s.name for s in syms if s['st_info']['type'] == 'STT_FUNC' and s['st_value']}
        selected = []
        for sym in syms:
            if sym['st_info']['type'] not in ('STT_FUNC', 'STT_OBJECT') or not any(term in sym.name for term in TERMS):
                continue
            if not isinstance(sym['st_shndx'], int):
                continue
            section = elf.get_section(sym['st_shndx'])
            offset = sym['st_value'] - section['sh_addr']
            data = section.data()[offset:offset + sym['st_size']]
            if len(data) != sym['st_size']:
                raise ValueError('Symbol extends beyond its section: ' + sym.name)
            item = dict(name=sym.name, address=sym['st_value'], size=sym['st_size'], section=section.name,
                        binding=sym['st_info']['bind'], type=sym['st_info']['type'], dynamically_exported=sym.name in exported,
                        bytes_sha256=hashlib.sha256(data).hexdigest(), first_16_bytes=data[:16].hex())
            if sym.name.startswith('_ZTV'):
                item['words'] = [dict(offset=i, value=struct.unpack_from('<I', data, i)[0],
                                      function=functions.get(struct.unpack_from('<I', data, i)[0]))
                                 for i in range(0, len(data) - 3, 4)]
            selected.append(item)
        return dict(schema_version=1, firmware='XDJ-XZ 1.26', rbp_sha256=digest, hardware_verified=False,
                    addresses='ELF virtual addresses, not file offsets', pointer_bytes=4,
                    symbols=sorted(selected, key=lambda x: (x['address'], x['name'])))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rbp', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = inventory(args.rbp)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    print(f"Inventoried {len(data['symbols'])} symbols; no firmware writes")

if __name__ == '__main__':
    main()
