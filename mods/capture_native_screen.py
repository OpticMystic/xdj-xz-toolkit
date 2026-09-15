"""Capture XZ framebuffer and read-only native screen state for layout qualification."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
from PIL import Image
from device_smoke import command
from live_io import HOST, transfer

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path, help='New evidence directory')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
pid = command(HOST, 'pidof rbp').strip()
if not re.fullmatch(r'[0-9]+', pid):
    raise RuntimeError('Expected one running application')
digest = command(HOST, f'md5sum /proc/{pid}/exe').split()[0]
if digest != '6a7ccb454e52afa26a73f3380706c9ca':
    raise RuntimeError('Expected the qualified XZ 1.26 application')
state = {'pid': int(pid), 'application_md5': digest, 'observation_only': True}
for name, address in [('gui_state_pointer', 0x1b26928), ('play_lifecycle_raw', 0x1b31ed0 + 0x7c),
                      ('window_property_index', 0x3c6fc7c), ('window_property_table', 0x3eba54c)]:
    page = base64.b64decode(''.join(command(HOST, f'dd if=/proc/{pid}/mem bs=4096 skip={address // 4096} count=1 2>/dev/null | base64').splitlines()), validate=True)
    if len(page) != 4096:
        raise RuntimeError('Incomplete read-only native state page')
    state[name] = int.from_bytes(page[address % 4096:address % 4096 + 4], 'little')
pixels = transfer('send', '/dev/fb0', 800 * 480 * 2)
state['framebuffer_sha256'] = hashlib.sha256(pixels).hexdigest()
state['pid_after'] = int(command(HOST, 'pidof rbp').strip())
if state['pid_after'] != state['pid']:
    raise RuntimeError('Application changed during capture')
Image.frombytes('RGB', (800, 480), pixels, 'raw', 'BGR;16').save(args.output / 'screen.png')
(args.output / 'state.json').write_text(json.dumps(state, indent=2) + '\n')
print(json.dumps(state, indent=2))
