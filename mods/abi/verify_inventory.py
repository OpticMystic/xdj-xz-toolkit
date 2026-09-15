from pathlib import Path
import json, struct, sys
sys.path.insert(0,str(Path(__file__).parent))
from inventory import inventory
root=Path(__file__).resolve().parents[2]
rbp=root/'vendor/decrypted_iso/pdj/extracted/pdj/rbp'
actual=inventory(rbp)
saved=json.loads((Path(__file__).parent/'xz-1.26-symbols.json').read_text())
assert actual==saved,'Generated inventory drifted'
layout=json.loads((Path(__file__).parent/'xz-1.26-layout.json').read_text())
v=next(s for s in actual['symbols'] if s['name']==layout['physical_key_vtable']['symbol'])
assert v['address']==layout['physical_key_vtable']['address']
w=next(w for w in v['words'] if w['offset']==16)
assert w['value']==layout['physical_key_vtable']['expected_function']
assert not any(s['dynamically_exported'] for s in actual['symbols'] if s['name'].startswith('_ZN2ui13PlayerInnards'))
print('PASS inventory reproducibility, exact vtable pointer, non-exported native dispatch')
