#!/usr/bin/env python3
"""Verified transactional update of a standalone ky-prism source tree."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

HERE=Path(__file__).resolve().parent
def sha(data): return hashlib.sha256(data).hexdigest()

def checked(root,name):
    part=Path(name)
    if not part.parts or part.is_absolute() or '..' in part.parts: raise ValueError('Unsafe path')
    path=root/part
    if any(p.is_symlink() for p in [path,*path.parents]): raise ValueError('Symlink refused: '+name)
    return path

def atomic(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    fd,tmp=tempfile.mkstemp(prefix='.prism-update-',dir=path.parent)
    try:
        with os.fdopen(fd,'wb') as stream:
            stream.write(data);stream.flush();os.fsync(stream.fileno())
        os.replace(tmp,path)
    finally: Path(tmp).unlink(missing_ok=True)

def apply(root):
    root=root.absolute();manifest=checked(root,'SOURCE-MANIFEST.json')
    before=manifest.read_bytes();rows=json.loads(before)
    for name,expected in rows.items():
        if sha(checked(root,name).read_bytes())!=expected:
            raise ValueError('Modified source preserved: '+name)
    patch=json.loads((HERE/'PATCH.json').read_text());changed={}
    for name,entry in patch.items():
        payload=checked(HERE/'payload',name).read_bytes();dst=checked(root,name)
        if sha(payload)!=entry['new']: raise ValueError('Corrupt update: '+name)
        if rows.get(name)==entry['new']: continue
        if rows.get(name) not in entry['old'] or (name not in rows and dst.exists()):
            raise ValueError('Unsupported/unmanaged source preserved: '+name)
        changed[name]=payload
    if not changed:
        print('Aktualizacja jest już zastosowana.');return
    backup=root/('.ky-prism-source-backup-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ'))
    backup.mkdir(mode=0o700)
    original={name:(root/name).read_bytes() if (root/name).exists() else None for name in changed}
    original['SOURCE-MANIFEST.json']=before
    for name,data in original.items():
        if data is not None:
            target=backup/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    (backup/'previous.json').write_text(json.dumps({name:data is not None for name,data in original.items()},indent=2)+'\n')
    written=[]
    try:
        for name,data in changed.items():
            written.append(name);atomic(root/name,data);rows[name]=sha(data)
        written.append('SOURCE-MANIFEST.json')
        atomic(manifest,(json.dumps(rows,indent=2)+'\n').encode())
    except BaseException:
        for name in reversed(written):
            data=original[name]
            if data is None: (root/name).unlink(missing_ok=True)
            else: atomic(root/name,data)
        raise
    print('Zaktualizowano źródła. Kopia poprzednich plików: '+str(backup))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    try: apply(parser.parse_args().source)
    except (OSError,ValueError) as error: sys.exit(str(error))
