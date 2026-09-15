# Private key directory — user-supplied only

This repository never ships boot keys. Place your local `aes256.key` here
(ignored by git) or pass `--key <path>` to the firmware tools.

The key file is a single line (up to 31 visible characters; padded to 32 bytes
by `builder/firmware.py::effective_key`). Never commit it, never publish it.
