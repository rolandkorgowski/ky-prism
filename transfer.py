#!/usr/bin/env python3
"""Private, portable application export; never copy Arch binaries to Ubuntu."""
import argparse
import datetime
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent

def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['export', 'backup'])
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    subprocess.run([sys.executable, '-B', ROOT/'package.py', 'check-source'], check=True)
    destination = args.destination.absolute()
    destination.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    folder = destination/('ky-prism-ubuntu-'+stamp)
    prepare = load(ROOT/'publication/prepare.py', 'prism_publication')
    prepare.stage(folder, personal=True, archive=False)
    transfer = load(folder/'tools/transfer.py', 'prism_transfer')
    info = {'target':'Ubuntu 26.04/26.04.1 amd64', 'kind':args.action,
            'source_system':'Omarchy', 'system_packages_included':False,
            'arch_binaries_included':False, 'photos_included':False}
    (folder/'TRANSFER-INFO.json').write_text(json.dumps(info,indent=2)+'\n')
    transfer.copy_preferences(folder)
    transfer.seal(folder)
    archive = transfer.export(folder, destination, args.action, refresh=False)
    os.chmod(folder,0o700)
    print(json.dumps({'archive':str(archive),'folder':str(folder),
                     'archive_verified':True,'dependencies':'Install from Ubuntu repositories; not bundled offline',
                     'install':'bash run.sh install --install-cuda --nvjpeg ON --test'},ensure_ascii=False,indent=2))

if __name__=='__main__':
    try: main()
    except (OSError, ValueError, subprocess.SubprocessError) as error: sys.exit(str(error))
