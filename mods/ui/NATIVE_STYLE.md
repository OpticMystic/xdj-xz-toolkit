# Native-style UI, local checkpoint

Reference: the actual XZ USB playback capture in
`artifacts/xdj-xz-mods/native-layout-usb-ready/screen.png`.

The original theme now uses black surfaces, grey outlines, pale blue selected
deck borders, angled deck tabs, an orange track strip, and white fader handles.
Muted stems have a persistent orange label. Unknown BPM uses a placeholder.
All five pages and seven themes remain available, with unsupported controls
marked NOT READY and receiver settings marked READ ONLY when appropriate.

Text uses a generated antialiased Latin-1 atlas derived from Barlow Semi
Condensed Medium. The font is a close visual substitute, not the proprietary
native XZ font. Source and SHA-256 are recorded in `fonts/source.json`; the SIL
Open Font License is in `fonts/OFL.txt`. The runtime needs no FreeType library,
font-file reads, or dynamic allocation for text. The atlas has 40,324 coverage
bytes. Private image packaging includes the font license notice.

## Repeatable local checks

From the repository root:

```powershell
python packages/xdj-xz-toolkit/mods/ui/verify.py --zig .tmp/cdj-zig/ziglang/zig.exe
python packages/xdj-xz-toolkit/mods/ui/preview.py --zig .tmp/cdj-zig/ziglang/zig.exe --output <new-directory>
python packages/xdj-xz-toolkit/mods/build.py --zig .tmp/cdj-zig/ziglang/zig.exe --output <new-build-directory>
```

`preview.py` compiles the real C renderer and produces an HTML image selector.
Its example track and connection states are fixtures. It never contacts the
device or VJ.Tools. The preview includes unavailable/external states and the
536x64 compact row, whose native placement is still unfinished.

The UI, touch, pad, compaction, atlas, accented-text and buffer-bound checks
passed locally. The ARM build and ABI checks passed. No device restart,
deployment, physical test or installed-runtime acceptance was performed for
this style change. The user requested offline development while VJ.Tools runs.
