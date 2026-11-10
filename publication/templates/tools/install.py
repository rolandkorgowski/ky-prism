#!/usr/bin/env python3
"""Native source installer. Package managers alone run with elevated privileges."""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from contextlib import contextmanager

ROOT = Path(__file__).resolve().parents[1]
ASSUME_YES = False
# GTK 4 window; the engine uses the non-visual Qt modules. Qt Widgets/Test are
# optional and only enable the legacy front end's regression tests.
MODULES = {'gtk4':'4.12','epoxy':'1','Qt6Core':'6.4','Qt6Gui':'6.4','Qt6Concurrent':'6.4',
           'vips-cpp':'8.12','libraw':'0.20','gl':'1','egl':'1','zlib':'1'}
COMMANDS = ('cmake','g++','make','pkg-config','lsblk','udisksctl','update-desktop-database')
APT = ['build-essential','cmake','pkg-config','python3','qt6-base-dev','qt6-base-dev-tools',
       'libqt6opengl6-dev','libgtk-4-dev','libepoxy-dev','libvips-dev','libraw-dev',
       'liblcms2-dev','libjpeg-dev','libtiff-dev','zlib1g-dev','libglvnd-dev',
       'libegl-dev','libgl-dev','libgl1-mesa-dri','util-linux','udisks2',
       'desktop-file-utils','xdg-utils','gsettings-desktop-schemas',
       'xdg-desktop-portal-gtk']
DNF = ['gcc-c++','make','cmake','pkgconf-pkg-config','python3','qt6-qtbase-devel',
       'gtk4-devel','libepoxy-devel','vips-devel','LibRaw-devel','lcms2-devel','libjpeg-turbo-devel',
       'libtiff-devel','zlib-devel','libglvnd-devel','mesa-libEGL-devel','mesa-libGL-devel',
       'mesa-dri-drivers','util-linux','udisks2','desktop-file-utils','xdg-utils',
       'xorg-x11-server-Xwayland']
ARCH = ['base-devel','cmake','pkgconf','python','qt6-base','gtk4','libepoxy','libvips',
        'libraw','libglvnd','libjpeg-turbo','lcms2','zlib','util-linux','udisks2',
        'desktop-file-utils','xdg-utils','xorg-xwayland']
KEYS = {
 '9': [('https://dl.fedoraproject.org/pub/epel/RPM-GPG-KEY-EPEL-9','FF8AD1344597106ECE813B918A3872BF3228467C'),
       ('https://rpms.remirepo.net/RPM-GPG-KEY-remi2021','B1ABF71E14C9D74897E198A8B19527F1478F8947')],
 '10':[('https://dl.fedoraproject.org/pub/epel/RPM-GPG-KEY-EPEL-10','7D8D15CBFC4E62688591FB2633D98517E37ED158'),
       ('https://rpms.remirepo.net/RPM-GPG-KEY-remi2024','CF1DF0057CE85DFF5B2F2A37C2FD3B2C2A0948E4')]
}

def run(command, **kwargs):
    print('+ '+shlex.join([str(x) for x in command]),flush=True)
    return subprocess.run([str(x) for x in command],check=True,**kwargs)

def system_info(text=None):
    values={}
    for line in (text if text is not None else Path('/etc/os-release').read_text()).splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            key,value=line.split('=',1)
            parsed=shlex.split(value,comments=True)
            values[key]=parsed[0] if parsed else ''
    distro,version=values.get('ID',''),values.get('VERSION_ID','')
    major=version.split('.')[0]
    if distro=='ubuntu' and re.fullmatch(r'(24|26)\.04(?:\.\d+)?',version): family='apt'
    elif distro in ('rhel','almalinux','rocky','centos') and major in ('9','10'): family='dnf'
    elif distro in ('arch','omarchy'): family='arch'
    else: raise ValueError('Supported: Ubuntu 24.04/26.04, RHEL 9/10 (also Alma/Rocky/CentOS 9/10), Omarchy/Arch. Found '+distro+' '+version)
    return dict(distro=distro,version=version,major=major,family=family)

def package_plan(info):
    if info['family']=='apt': return ['apt-get','install','--no-install-recommends',*APT]
    if info['family']=='dnf': return ['dnf','install','--setopt=install_weak_deps=False',*DNF]
    return ['pacman','-S','--needed',*ARCH]

def missing():
    result=[name for name in COMMANDS if not shutil.which(name)]
    if shutil.which('pkg-config'):
        for name,version in MODULES.items():
            if subprocess.run(['pkg-config','--atleast-version='+version,name],capture_output=True).returncode:
                result.append(name+'>='+version)
    else: result.extend(MODULES)
    return result

def privileged(command, **kwargs):
    command=list(command)
    if ASSUME_YES and command[0] in ('apt-get','dnf'): command.insert(1,'-y')
    return run(([] if os.geteuid()==0 else ['sudo'])+command,**kwargs)

def key_fingerprint(path,home):
    result=subprocess.run(['gpg','--no-options','--homedir',str(home),'--batch','--with-colons','--show-keys',str(path)],check=True,text=True,capture_output=True)
    lines=result.stdout.splitlines()
    if sum(line.startswith('pub:') for line in lines)!=1: raise ValueError('Unexpected signing-key count')
    return next(line.split(':')[9] for line in lines if line.startswith('fpr:'))

def setup_rhel(info):
    if info['family']!='dnf': raise ValueError('--setup-rhel-repos is only for Enterprise Linux 9/10')
    if platform.machine() not in ('x86_64','amd64'): raise ValueError('Automatic Remi setup is provided for x86_64; configure repositories manually on other architectures.')
    major=info['major']
    privileged(['dnf','install','gnupg2','dnf-plugins-core','ca-certificates'])
    if info['distro']=='rhel':
        privileged(['subscription-manager','repos','--enable=codeready-builder-for-rhel-'+major+'-'+platform.machine()+'-rpms'])
    else: privileged(['dnf','config-manager','--set-enabled','crb'])
    # Only explicit --setup-rhel-repos reaches this path. Trust is anchored to
    # fingerprints published by Fedora and Remi, not an arbitrary downloaded key.
    with tempfile.TemporaryDirectory(prefix='ky-prism-keys-') as tmp:
        directory=Path(tmp);os.chmod(directory,0o700)
        for index,(url,expected) in enumerate(KEYS[major]):
            with urllib.request.urlopen(url,timeout=30) as response:
                if not response.geturl().startswith('https://'): raise ValueError('Signing key redirected away from HTTPS')
                data=response.read(512*1024+1)
            if len(data)>512*1024: raise ValueError('Oversized signing key')
            key=directory/('key-'+str(index));key.write_bytes(data)
            if key_fingerprint(key,directory)!=expected: raise ValueError('Signing key fingerprint mismatch: '+url)
            privileged(['rpm','--import',key])
    privileged(['dnf','install','--setopt=localpkg_gpgcheck=1',
                'https://dl.fedoraproject.org/pub/epel/epel-release-latest-'+major+'.noarch.rpm',
                'https://rpms.remirepo.net/enterprise/remi-release-'+major+'.rpm'])

def dependencies(info):
    needed=missing()+missing_desktop_packages(info)
    if not needed: return
    print('Missing: '+', '.join(needed),flush=True)
    if info['family']=='apt': privileged(['apt-get','update'])
    try: privileged(package_plan(info))
    except subprocess.CalledProcessError:
        if info['family']=='dnf':
            print('Enterprise Linux needs CRB, EPEL and Remi-safe for libvips. See docs/RHEL.md or use --setup-rhel-repos.',file=sys.stderr)
        raise
    if missing(): raise ValueError('Dependencies still unavailable: '+', '.join(missing()))

def missing_desktop_packages(info):
    if info['family'] != 'apt': return []
    result=[]
    # pkg-config checks development libraries, not the runtime pieces a
    # desktop session needs (GL driver, icon/settings data, portal chooser).
    for name in ('libgtk-4-1','libgl1-mesa-dri','gsettings-desktop-schemas','xdg-desktop-portal-gtk','xdg-utils'):
        found=subprocess.run(['dpkg-query','-W','-f=${db:Status-Status}',name],capture_output=True,text=True)
        if found.returncode or found.stdout.strip()!='installed': result.append(name)
    return result

def install_cuda(info):
    if info['family'] != 'apt':
        raise ValueError('--install-cuda supports Ubuntu only; install the local CUDA Toolkit separately on other systems.')
    # Use Ubuntu's signed repositories, never a foreign release or a runfile.
    # This is opt-in; it does not select/install a GPU driver or change PRIME.
    privileged(['apt-get', 'update'])
    privileged(['apt-get', 'install', '--no-install-recommends', '--no-remove', cuda_package(info)])

def cuda_package(info):
    # Ubuntu 26.04 provides the newer modular NVIDIA SDK in Multiverse.
    return 'cuda-toolkit' if info.get('version','').startswith('26.04') else 'nvidia-cuda-toolkit'

def build_jobs():
    cores = len(os.sched_getaffinity(0)) if hasattr(os,'sched_getaffinity') else (os.cpu_count() or 2)
    memory = os.sysconf('SC_PHYS_PAGES') * os.sysconf('SC_PAGE_SIZE')
    return max(1, min(8, cores // 2, memory // (3 * 1024**3)))

def digest(path):
    with Path(path).open('rb') as stream: return hashlib.file_digest(stream,'sha256').hexdigest() if hasattr(hashlib,'file_digest') else _legacy_digest(stream)

def _legacy_digest(stream):
    value=hashlib.sha256()
    for chunk in iter(lambda:stream.read(1024*1024),b''): value.update(chunk)
    return value.hexdigest()

def safe_name(name):
    p=Path(name)
    if p.is_absolute() or '..' in p.parts or not p.parts or any(ord(c)<32 for c in name): raise ValueError('Unsafe installation path')
    return p

def no_links(path):
    if any(p.is_symlink() for p in [path,*path.parents]): raise ValueError('Symlink in installation path: '+str(path))

def atomic(path,data,mode=0o644):
    path.parent.mkdir(parents=True,exist_ok=True)
    fd,tmp=tempfile.mkstemp(prefix='.ky-prism-',dir=path.parent)
    try:
        with os.fdopen(fd,'wb') as stream:
            stream.write(data);stream.flush();os.fsync(stream.fileno());os.fchmod(stream.fileno(),mode)
        os.replace(tmp,path)
    finally: Path(tmp).unlink(missing_ok=True)

def receipt(prefix):
    p=prefix/'share/ky-prism/install.json';no_links(p)
    if not p.exists(): return {}
    if p.stat().st_size>4*1024*1024: raise ValueError('Oversized installation receipt')
    value=json.loads(p.read_text())
    if not isinstance(value,dict): raise ValueError('Invalid installation receipt')
    for name,sha in value.items():
        safe_name(name)
        if not isinstance(sha,str) or len(sha)!=64: raise ValueError('Invalid installation checksum')
    return value

@contextmanager
def install_lock(prefix):
    path=prefix/'share/ky-prism/install.lock';no_links(path)
    path.parent.mkdir(parents=True,exist_ok=True)
    fd=os.open(path,os.O_CREAT|os.O_RDWR|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,'a') as stream:
        fcntl.flock(stream,fcntl.LOCK_EX|fcntl.LOCK_NB)
        yield

def apply_stage(stage,prefix):
    prefix=prefix.absolute();no_links(prefix)
    old=receipt(prefix)
    files={str(p.relative_to(stage)):p for p in stage.rglob('*') if p.is_file()}
    for name in set(files)|set(old):
        p=prefix/safe_name(name);no_links(p)
        if p.exists() and (not p.is_file() or name not in old or digest(p)!=old[name]):
            raise ValueError('Existing unmanaged/modified file preserved: '+str(p))
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    backup=prefix/'share/ky-prism/install-backups'/stamp;backup.mkdir(parents=True,exist_ok=False)
    previous={};written=[]
    try:
        for name in set(files)|set(old):
            p=prefix/name
            previous[name]=p.exists()
            if p.exists():
                b=backup/name;b.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,b)
            written.append(name)
            if name in files:
                src=files[name];atomic(p,src.read_bytes(),src.stat().st_mode&0o777)
            else: p.unlink(missing_ok=True)
        values={name:digest(prefix/name) for name in files}
        atomic(prefix/'share/ky-prism/install.json',(json.dumps(values,indent=2)+'\n').encode(),0o600)
    except BaseException:
        for name in reversed(written):
            p=prefix/name
            if previous[name]:
                b=backup/name;atomic(p,b.read_bytes(),b.stat().st_mode&0o777)
            else: p.unlink(missing_ok=True)
        raise
    print('Installed. Previous managed files: '+str(backup))

def uninstall(prefix):
    old=receipt(prefix)
    if not old: raise ValueError('No managed installation found at '+str(prefix))
    for name,sha in old.items():
        p=prefix/name;no_links(p)
        if p.exists() and (not p.is_file() or digest(p)!=sha): raise ValueError('Modified file preserved: '+str(p))
    for name in old: (prefix/name).unlink(missing_ok=True)
    (prefix/'share/ky-prism/install.json').unlink()
    print('Uninstalled program files; photo files and user preferences preserved.')

def verify_source():
    manifest=ROOT/'SOURCE-MANIFEST.json'
    if not manifest.exists(): return # Git contributors may build edited sources.
    rows=json.loads(manifest.read_text())
    for name,sha in rows.items():
        p=ROOT/safe_name(name)
        if p.is_symlink() or digest(p)!=sha: raise ValueError('Source checksum mismatch: '+name)

def main():
    global ASSUME_YES
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true',help='Dependency and source checks without writes')
    parser.add_argument('--plan',action='store_true',help='Print package commands without writes')
    parser.add_argument('--dependencies-only',action='store_true')
    parser.add_argument('--no-deps',action='store_true')
    parser.add_argument('--yes',action='store_true',help='Accept package manager prompts (for automation)')
    parser.add_argument('--setup-rhel-repos',action='store_true',help='Explicitly enable CRB, EPEL and Remi-safe; import fingerprint-verified keys')
    parser.add_argument('--nvjpeg',choices=['AUTO','ON','OFF'],type=str.upper,default='AUTO')
    parser.add_argument('--install-cuda',action='store_true',help='Ubuntu: install optional CUDA SDK from configured repositories (sudo)')
    parser.add_argument('--cuda-architectures',help='CMake CUDA targets, e.g. 89-real;89-virtual for RTX 4070')
    parser.add_argument('--test',action='store_true',help='Build and run regression tests before installation')
    parser.add_argument('--jobs',type=int,default=build_jobs())
    parser.add_argument('--prefix',type=Path,default=Path.home()/'.local')
    parser.add_argument('--uninstall',action='store_true')
    args=parser.parse_args()
    if args.install_cuda and args.nvjpeg == 'OFF': parser.error('--install-cuda conflicts with --nvjpeg OFF')
    if args.cuda_architectures and not re.fullmatch(r'\d+(?:-(?:real|virtual))?(?:;\d+(?:-(?:real|virtual))?)*',args.cuda_architectures):
        parser.error('Invalid CUDA architectures')
    ASSUME_YES=args.yes
    if not 1<=args.jobs<=32: parser.error('--jobs must be 1..32')
    if sys.maxsize<=2**32: raise ValueError('ky-prism requires a 64-bit userspace.')
    if args.uninstall:
        if os.geteuid()==0: raise ValueError('Run uninstall as the desktop user, without sudo.')
        with install_lock(args.prefix.absolute()): uninstall(args.prefix.absolute())
        return
    info=system_info();verify_source()
    print('Platform: '+info['distro']+' '+info['version'])
    if args.plan:
        print(shlex.join(package_plan(info)))
        if args.install_cuda: print('apt-get install --no-install-recommends --no-remove '+cuda_package(info))
        return
    if args.check:
        needed=missing()+missing_desktop_packages(info);print('Missing: '+(', '.join(needed) if needed else 'none'))
        if needed: sys.exit(2)
        return
    if not args.dependencies_only:
        if os.geteuid()==0: raise ValueError('Run ./install.sh as the desktop user, without sudo; only dependency commands use sudo.')
        prefix=args.prefix.absolute();no_links(prefix)
        old=receipt(prefix);launcher=prefix/'bin/ky-prism';no_links(launcher)
        if launcher.exists() and ('bin/ky-prism' not in old or not launcher.is_file() or digest(launcher)!=old['bin/ky-prism']):
            raise ValueError('Existing unmanaged/modified launcher preserved; use another --prefix: '+str(launcher))
    if args.setup_rhel_repos: setup_rhel(info)
    if not args.no_deps: dependencies(info)
    if args.install_cuda: install_cuda(info)
    if missing(): raise ValueError('Missing: '+', '.join(missing()))
    if args.dependencies_only: return
    prefix=args.prefix.absolute();no_links(prefix)
    build=ROOT/'build';no_links(build)
    configure=['cmake','-S',ROOT,'-B',build,'-DCMAKE_BUILD_TYPE=Release',
               '-DBUILD_TESTING='+('ON' if args.test else 'OFF'),'-DPRISM_WITH_NVJPEG='+args.nvjpeg]
    if args.cuda_architectures: configure.append('-DCMAKE_CUDA_ARCHITECTURES='+args.cuda_architectures)
    run(configure)
    run(['cmake','--build',build,'--parallel',str(args.jobs)])
    # The live-window test depends on the running compositor (workspace,
    # tiling, focus); it must never decide whether the program can be installed.
    if args.test: run(['ctest','--test-dir',build,'--output-on-failure','-LE','display'])
    if args.nvjpeg == 'ON':
        # Requiring the SDK alone is not proof that the driver/device can decode.
        run([build/'prism-nvjpeg-test','--require-nvjpeg'])
    with tempfile.TemporaryDirectory(prefix='ky-prism-stage-') as tmp:
        stage=Path(tmp)
        run(['cmake','--install',build,'--prefix',stage])
        desktop=stage/'share/applications/ky-prism.desktop'
        command=str(prefix/'bin/ky-prism').replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$').replace('%','%%')
        desktop.write_text(desktop.read_text().replace('Exec=ky-prism %f','Exec="'+command+'" %f'))
        with install_lock(prefix): apply_stage(stage,prefix)
    if shutil.which('update-desktop-database'):
        subprocess.run(['update-desktop-database',str(prefix/'share/applications')],check=True)
    print('Start: '+str(prefix/'bin/ky-prism'))
    if str(prefix/'bin') not in os.environ.get('PATH','').split(os.pathsep): print('Add '+str(prefix/'bin')+' to PATH, or log out/in after installing to ~/.local.')

if __name__=='__main__':
    try: main()
    except (OSError,ValueError,subprocess.SubprocessError) as error: sys.exit(str(error))
