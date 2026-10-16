#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description="Package a clean-commit GPU tile installer test build")
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
if subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True):
    raise SystemExit("Commit the source tree and rebuild before packaging")
manifest = json.loads((root / "dist/manifest.json").read_text())
if manifest.get("sourceCommit") != commit or manifest.get("sourceDirty") is not False or manifest.get("gpuBuild") is not True:
    raise SystemExit("Rebuild the native GPU package and manifest from the clean final commit")
folder = root / "dist/PPSA99082"
files = {}
for path in sorted(folder.rglob("*")):
    if path.is_symlink():
        raise SystemExit(f"Package contains a symlink: {path}")
    if path.is_file():
        files[path.relative_to(folder).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
if files != manifest.get("files"):
    raise SystemExit("Package contents no longer match the native build manifest")
required = {
    "eboot.bin", "sce_module/libc.prx", "sce_sys/param.json",
    "opennow-tile-installer.elf", "assets/cacert.pem",
    "assets/forwarder-template/eboot.bin", "assets/forwarder-template/sce_module/libc.prx",
    "assets/forwarder-template/LICENSE", "assets/forwarder-template/UPSTREAM.md",
    "assets/fonts/Nunito-SemiBold.ttf", "assets/fonts/Nunito-Bold.ttf",
    "assets/fonts/Nunito-ExtraBold.ttf", "assets/fonts/Nunito-Black.ttf",
    "assets/fonts/OpenNOWMono-Medium.ttf",
}
if not required <= files.keys() or any("elevator" in name for name in files):
    raise SystemExit("Installer, template, fonts, certificates, or license inventory is invalid")
pins = json.loads((root / "upstream-lock.json").read_text())
for path, pin in {
    "assets/forwarder-template/eboot.bin": "ps5-forwarder-template-eboot-sha256",
    "assets/forwarder-template/sce_module/libc.prx": "ps5-forwarder-template-libc-sha256",
}.items():
    if files[path] != pins[pin]:
        raise SystemExit(f"Packaged template differs from its source pin: {path}")
validate = runpy.run_path(str(root / "tools/validate-elevation-helper.py"))["validate"]
validate((folder / "opennow-tile-installer.elf").read_bytes())
subprocess.run(["python3", "tools/audit-imports.py"], cwd=root, check=True)
manifest["testConfiguration"] = "OPENNOW_GPU=1; independent tile installer; console unverified"
support = {name: (root / name).read_bytes() for name in (
    "LICENSE", "THIRD_PARTY_NOTICES.md", "upstream-lock.json",
    "docs/TITLE_FORWARDING.md", "docs/TILE_INSTALLER_RESOLVER.md",
)}
manifest["supportFiles"] = {name: hashlib.sha256(data).hexdigest() for name, data in support.items()}
text = json.dumps(manifest, indent=2) + "\n"
output = args.output.expanduser().resolve()
output.mkdir(parents=True, exist_ok=True)
archive = output / f"OpenNOW-PS5-GPU-tiles-{commit[:12]}.zip"
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
    for name in files:
        path = folder / name
        info = zipfile.ZipInfo("PPSA99082/" + name)
        info.external_attr = path.stat().st_mode << 16
        zipped.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    info = zipfile.ZipInfo("manifest.json")
    info.external_attr = 0o100644 << 16
    zipped.writestr(info, text, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    for name, data in support.items():
        info = zipfile.ZipInfo(name)
        info.external_attr = 0o100644 << 16
        zipped.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
(output / "manifest.json").write_text(text)
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(output / "SHA256SUMS").write_text(f"{digest}  {archive.name}\n")
with zipfile.ZipFile(archive) as zipped:
    assert zipped.testzip() is None
    assert json.loads(zipped.read("manifest.json"))["sourceCommit"] == commit
    for name, expected in files.items():
        assert hashlib.sha256(zipped.read("PPSA99082/" + name)).hexdigest() == expected
    for name, expected in manifest["supportFiles"].items():
        assert hashlib.sha256(zipped.read(name)).hexdigest() == expected
print(f"{digest}  {archive}")
