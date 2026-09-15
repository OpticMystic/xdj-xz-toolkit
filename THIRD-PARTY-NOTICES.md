# Third-party notices

Original code in this repository is MIT-licensed (see `LICENSE`).

Retain the following with any redistribution:

- `mods/key/` receiver/key-shifter files: MPL-2.0 (`mods/key/LICENSE-MPL-2.0`,
  `mods/integration/NOTICE-MPL-2.0.txt`).
- `mods/audio/licenses/`: MIT / Apache-2.0 notices for the cache/mixer code.
- `mods/audio/vendor/dr_libs/`: public-domain-style `LICENSE` + `UPSTREAM.txt`.
- `mods/ui/fonts/`: `OFL.txt` (Barlow Semi Condensed) + `source.json`.
- `vendor/build/directfb-1.4-src/`: LGPL-2.1-only headers (see
  `vendor/build/directfb-1.4-src/PROVENANCE.md`).
- `builder/models.json`: per-checkpoint original-source MIT grants for the
  approved separation presets (Open-Unmix UMX-HQ, Smule Windowed RoFormer).
  Demucs research-only weights, UMXL non-commercial weights and unclear model
  mirrors are not approved presets — model weights are never shipped here and
  are downloaded separately from their original publishers when requested.
- Python dependencies (`requirements.txt`: `cryptography`, `pycdlib`) keep
  their own licenses; the XZ Mods resource builder records their notices at
  package time (see XZ-Mods `BUILDING.md`).
