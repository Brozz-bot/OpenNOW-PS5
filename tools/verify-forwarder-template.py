#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the exact shared programs required by PS5 Forwarder Format v1."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
lock = json.loads((root / "upstream-lock.json").read_text())
for name, key in (("eboot.bin", "ps5-forwarder-template-eboot-sha256"),
                  ("sce_module/libc.prx", "ps5-forwarder-template-libc-sha256")):
    path = root / "assets/forwarder-template" / name
    if hashlib.sha256(path.read_bytes()).hexdigest() != lock[key]:
        raise SystemExit(f"Forwarder template hash mismatch: {name}")
print("PS5 forwarder template: both pinned SHA-256 hashes verified.")
