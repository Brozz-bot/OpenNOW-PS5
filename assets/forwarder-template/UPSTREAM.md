# Shared forwarder template

These files are copied byte-for-byte from `template/` in
[PS5 Forwarder Format](https://github.com/heydemoura/ps5-forwarder-format),
revision `22e8762ad7dbd01e18bd8c0e95f86461fdf8aaf4`, GPL-3.0-or-later,
Copyright (C) 2026 heydemoura and the credited runtime authors.

`eboot.bin` contains the standard launcher. No separate resident launcher is
required when elfldr is available on loopback port 9021. The binary files are
intentional vendored inputs, not local build outputs. Corresponding format,
library, forwarder and launcher source is in `vendor/ps5-forwarder-format`.
The clean-room libc source is in `tooling/native/` in this repository and has
the identical signed hash. See `THIRD_PARTY_NOTICES.md` for provenance.

`tools/verify-forwarder-template.py` checks both files against the SHA-256 pins
in `upstream-lock.json`. The writer checks them again before creating a tile.
