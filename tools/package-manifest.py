#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import subprocess
root=Path(__file__).resolve().parents[1]
folder=root/'dist/PPSA99082'
files={str(path.relative_to(folder)):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(folder.rglob('*')) if path.is_file()}
if not files or 'eboot.bin' not in files:
    raise SystemExit('Native package missing')
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True))
gpu='-DOPENNOW_GPU=1' in (root/'build/app.ninja').read_text()
(root/'dist/manifest.json').write_text(json.dumps({'titleId':'PPSA99082','version':json.loads((folder/'sce_sys/param.json').read_text())['contentVersion'],'stage':'development','sourceCommit':commit,'sourceDirty':dirty,'gpuBuild':gpu,'consoleTested':False,'files':files},indent=2)+'\n')
print(f'Manifest written: {len(files)} files')
