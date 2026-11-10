#!/usr/bin/env python3
"""Verified source transfers and local hardware diagnostics (no telemetry)."""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PREFERENCES = {'favorites.json':8*1024**2, 'bookmarks.json':128*1024, 'language.json':4096, 'appearance.json':4096}

def digest(path):
    value=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024**2),b''): value.update(chunk)
    return value.hexdigest()

def no_links(path):
    if any(p.is_symlink() for p in [path,*path.parents]):
        raise ValueError('Symlink refused: '+str(path))

def safe_file(root, name):
    p=Path(name)
    if p.is_absolute() or '..' in p.parts or not p.parts or any(ord(c)<32 for c in name):
        raise ValueError('Unsafe path: '+name)
    path=root/p
    no_links(path)
    return path

def verify(root):
    rows=json.loads((root/'SOURCE-MANIFEST.json').read_text())
    if not isinstance(rows,dict) or not rows: raise ValueError('Empty source manifest')
    for name,sha in rows.items():
        p=safe_file(root,name)
        if not p.is_file() or digest(p)!=sha: raise ValueError('Checksum mismatch: '+name)
    return rows

def seal(root):
    rows={}
    for path in sorted(root.rglob('*')):
        no_links(path)
        if path.is_file() and path.name!='SOURCE-MANIFEST.json':
            rows[str(path.relative_to(root))]=digest(path)
    (root/'SOURCE-MANIFEST.json').write_text(json.dumps(rows,indent=2)+'\n')

def data_directory():
    return Path(os.environ.get('XDG_DATA_HOME',str(Path.home()/'.local/share')))/'omarchy-prism'

def preference_directory(name):
    if name=='appearance.json':
        return Path(os.environ.get('XDG_CONFIG_HOME',str(Path.home()/'.config')))/'ky-prism'
    return data_directory()

def preference(path, limit):
    no_links(path)
    if not path.is_file() or path.stat().st_size>limit: raise ValueError('Invalid preference: '+path.name)
    data=path.read_bytes()
    if len(data)>limit: raise ValueError('Oversized preference')
    value=json.loads(data)
    if path.name=='appearance.json':
        percent=value.get('ui_opacity_percent') if isinstance(value,dict) else None
        if type(percent) is not int or not 0<=percent<=100: raise ValueError('Invalid UI opacity')
        return (json.dumps({'ui_opacity_percent':percent})+'\n').encode()
    if path.name=='language.json':
        if not isinstance(value,dict) or value.get('version')!=1 or value.get('language') not in ('pl','en'):
            raise ValueError('Invalid language preference')
        return (json.dumps({'version':1,'language':value['language']})+'\n').encode()
    if not isinstance(value,(dict,list)): raise ValueError('Invalid preference JSON')
    return data

def copy_preferences(folder):
    for name,limit in PREFERENCES.items():
        src=preference_directory(name)/name
        if src.exists() or src.is_symlink():
            payload=preference(src,limit)
            target=folder/'user-data'/name
            no_links(target)
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(payload)
            target.chmod(0o600)

def audit_archive(archive):
    with tarfile.open(archive,'r:gz') as tar:
        members=tar.getmembers()
        names=[m.name for m in members]
        if len(names)!=len(set(names)): raise ValueError('Duplicate archive members')
        rows=json.load(tar.extractfile('ky-prism/SOURCE-MANIFEST.json'))
        files=set()
        for member in members:
            name=Path(member.name)
            if name.is_absolute() or '..' in name.parts or name.parts[0]!='ky-prism' or not (member.isdir() or member.isfile()):
                raise ValueError('Unsafe archive entry')
            if not member.isfile() or member.name=='ky-prism/SOURCE-MANIFEST.json': continue
            relative=str(name.relative_to('ky-prism'));files.add(relative)
            value=hashlib.sha256()
            with tar.extractfile(member) as stream:
                for chunk in iter(lambda:stream.read(1024**2),b''): value.update(chunk)
            if rows.get(relative)!=value.hexdigest(): raise ValueError('Archive mismatch: '+relative)
        if files!=set(rows): raise ValueError('Incomplete archive')
    return len(files)

def export(root, destination, kind='export', *, refresh=True):
    rows=verify(root)
    destination=destination.absolute();no_links(destination)
    destination.mkdir(parents=True,exist_ok=True)
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    final=destination/('ky-prism-'+kind+'-'+stamp+'.tar.gz')
    # Only manifest-listed sources are transferred, never build/, .git or binaries.
    with tempfile.TemporaryDirectory(prefix='.prism-transfer-',dir=destination) as tmp:
        folder=Path(tmp)/'ky-prism';folder.mkdir()
        for name in rows:
            src=safe_file(root,name);target=folder/name
            target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,target)
            if digest(target)!=rows[name]: raise ValueError('Source changed during export: '+name)
        if refresh: copy_preferences(folder)
        seal(folder)
        pending=Path(tmp)/'verified.tar.gz'
        with tarfile.open(pending,'x:gz') as tar: tar.add(folder,arcname='ky-prism')
        pending.chmod(0o600)
        audit_archive(pending)
        sha=digest(pending)
        os.replace(pending,final)
        final.with_suffix('.gz.sha256').write_text(sha+'  '+final.name+'\n')
    return final

def restore(root):
    verify(root)
    pending={}
    for name,limit in PREFERENCES.items():
        src=root/'user-data'/name
        if not src.exists(): continue
        payload=preference(src,limit)
        dst=preference_directory(name)/name;no_links(dst)
        if dst.exists():
            print('Preserved existing preference: '+name)
        else: pending[name]=payload
    written=[]
    try:
        for name,payload in pending.items():
            dst=preference_directory(name)/name
            dst.parent.mkdir(parents=True,exist_ok=True)
            with os.fdopen(os.open(dst,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600),'wb') as stream:
                written.append(dst);stream.write(payload);stream.flush();os.fsync(stream.fileno())
    except BaseException:
        for path in written: path.unlink(missing_ok=True)
        raise
    print('Restored: '+(', '.join(pending) or 'nothing'))
    print('Photo files are separate. Original paths are preserved; use Locate for moved favourites.')

def capture(command, timeout=15):
    try:
        value=subprocess.run([str(x) for x in command],text=True,capture_output=True,timeout=timeout)
        return {'exit_code':value.returncode,'stdout':value.stdout[-32000:],'stderr':value.stderr[-4000:]}
    except (OSError,subprocess.TimeoutExpired) as error: return {'exit_code':None,'error':str(error)}

def doctor(prefix, gpu=False):
    cpu=Path('/proc/cpuinfo').read_text() if Path('/proc/cpuinfo').exists() else ''
    flags=set(next((line.split(':',1)[1] for line in cpu.splitlines() if line.startswith('flags')), '').split())
    report={'cpu_model':next((line.split(':',1)[1].strip() for line in cpu.splitlines() if line.startswith('model name')),'unknown'),
            'cpu_instructions':{f:f in flags for f in ('sse4_2','avx2','fma','avx512f','avx512bw','avx512vl')},
            'cpu_dispatch':'AVX2 when supported by CPU/OS, scalar otherwise; no global -march=native',
            'packages_bundled_offline':False}
    report['nvidia']=capture(['nvidia-smi','--query-gpu=name,driver_version,memory.total,compute_cap','--format=csv,noheader'])
    executable=prefix/'libexec/ky-prism/prism-nvjpeg-test'
    if not executable.is_file(): executable=ROOT/'build/prism-nvjpeg-test'
    report['decode_test']=capture([executable,'--require-nvjpeg' if gpu else '--cpu'],timeout=90)
    print(json.dumps(report,ensure_ascii=False,indent=2))
    return 0 if report['decode_test']['exit_code']==0 else 2

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['export','backup','restore-data','verify','doctor'])
    parser.add_argument('destination',type=Path,nargs='?')
    parser.add_argument('--prefix',type=Path,default=Path.home()/'.local')
    parser.add_argument('--gpu',action='store_true')
    args=parser.parse_args()
    if args.action in ('export','backup'):
        if args.destination is None: parser.error('Destination directory required')
        print(export(ROOT,args.destination,args.action))
    elif args.action=='restore-data': restore(ROOT)
    elif args.action=='verify': print('Verified source files: '+str(len(verify(ROOT))))
    else: sys.exit(doctor(args.prefix,args.gpu))

if __name__=='__main__':
    try: main()
    except (OSError,ValueError,tarfile.TarError) as error: sys.exit(str(error))
