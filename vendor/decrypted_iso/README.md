# Private firmware inputs — user-supplied only

This repository never ships Pioneer/AlphaTheta firmware, decrypted images, or
extracted `rbp` application binaries.

Supported flow (`builder/firmware.py::import_application`):

1. Download the official XDJ-XZ v1.26 ZIP from the publisher:
   https://downloads.support.alphatheta.com/firmwares/all-in-one-dj-systems/XDJ-XZ/XDJXZ_v126.zip
2. Extract it locally and select the `XDJXZ.UPD` updater (or the outer ZIP —
   the importer selects the single `.UPD` member).
3. Supply your local boot key separately (`--key <aes256.key>`).

Only the exact 1.26 archive identity pinned in `builder/firmware.py`
(`STOCK_SHA` / `PATCHED_MD5`) is accepted. Unknown versions fail closed.
