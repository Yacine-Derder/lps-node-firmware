#!/usr/bin/env python3
"""Export a committed canonical core into an explicit destination; no fetch/build side effects."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('revision');p.add_argument('destination',type=Path)
p.add_argument('--allow-local',action='store_true',help='Explicitly export an unpublished local commit; no push/fetch')
a=p.parse_args();root=Path(__file__).resolve().parents[1]
def git(*args): return subprocess.check_output(['git',*args],cwd=root)
revision=git('rev-parse',a.revision+'^{commit}').decode().strip()
if not a.allow_local:
    subprocess.run(['git','merge-base','--is-ancestor',revision,'origin/master'],cwd=root,check=True)
prefix='portable/mul_core/'
files=git('ls-tree','-r','--name-only',revision,'--',prefix).decode().splitlines()
if not files: raise SystemExit('No canonical core in this commit')
if a.destination.exists() and any(a.destination.iterdir()):
    raise SystemExit('Destination must be absent or empty; verify/review old snapshot before replacing it')
hashes={}
for name in files:
    relative=name.removeprefix(prefix);data=git('show',revision+':'+name)
    target=a.destination/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    hashes[relative]=hashlib.sha256(data).hexdigest()
(a.destination/'UPSTREAM.json').write_text(json.dumps(dict(url='https://github.com/Yacine-Derder/lps-node-firmware.git',revision=revision,path=prefix.rstrip('/'),sha256=hashes),indent=2)+'\n')
print('Exported '+revision+' to '+str(a.destination))
