#!/usr/bin/env python3
"""Dependency preflight. Installs only missing requirements using distro packages."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

BASE = ['cmake', 'pkg-config', 'g++', 'make', 'python3', 'lsblk', 'udisksctl', 'update-desktop-database']
# The window is GTK 4; the decoder/cache engine uses the non-visual Qt modules.
# Qt Widgets/Test are optional: only the legacy front end and its tests use them.
MODULES = ['gtk4', 'epoxy', 'Qt6Core', 'Qt6Gui', 'Qt6Concurrent', 'vips-cpp', 'libraw', 'egl', 'gl', 'zlib']

def missing():
    result = [name for name in BASE if not shutil.which(name)]
    if shutil.which('pkg-config'):
        result += [name for name in MODULES if subprocess.run(['pkg-config', '--exists', name], capture_output=True).returncode]
        if subprocess.run(['pkg-config', '--atleast-version=6.4', 'Qt6Core'], capture_output=True).returncode:
            result.append('Qt >= 6.4')
        if subprocess.run(['pkg-config', '--atleast-version=4.12', 'gtk4'], capture_output=True).returncode:
            result.append('GTK >= 4.12')
    else:
        result += MODULES
    return result

def plan():
    values = {}
    for line in Path('/etc/os-release').read_text().splitlines():
        if '=' in line:
            k, v = line.split('=', 1); values[k] = v.strip('"')
    ids = {values.get('ID', ''), *values.get('ID_LIKE', '').split()}
    if ids & {'arch', 'omarchy'}:
        return ['pacman', '-S', '--needed', 'base-devel', 'cmake', 'pkgconf', 'python', 'qt6-base', 'gtk4', 'libepoxy', 'libvips', 'libraw', 'libglvnd', 'libjpeg-turbo', 'zlib', 'lcms2', 'util-linux', 'udisks2', 'desktop-file-utils', 'xdg-utils']
    if ids & {'debian', 'ubuntu'}:
        return ['apt-get', 'install', '--no-install-recommends', 'build-essential', 'cmake', 'pkg-config', 'python3', 'qt6-base-dev', 'qt6-base-dev-tools', 'libqt6opengl6-dev', 'libgtk-4-dev', 'libepoxy-dev', 'libvips-dev', 'libraw-dev', 'liblcms2-dev', 'libjpeg-dev', 'zlib1g-dev', 'libglvnd-dev', 'libegl-dev', 'libgl-dev', 'util-linux', 'udisks2', 'desktop-file-utils', 'xdg-utils']
    if ids & {'fedora'}:
        return ['dnf', 'install', '--setopt=install_weak_deps=False', 'gcc-c++', 'make', 'cmake', 'pkgconf-pkg-config', 'python3', 'qt6-qtbase-devel', 'gtk4-devel', 'libepoxy-devel', 'vips-devel', 'LibRaw-devel', 'lcms2-devel', 'libjpeg-turbo-devel', 'zlib-devel', 'libglvnd-devel', 'mesa-libEGL-devel', 'mesa-libGL-devel', 'util-linux', 'udisks2', 'desktop-file-utils', 'xdg-utils']
    raise RuntimeError('Obsługiwane: Omarchy/Arch, Ubuntu >=24.04, Debian >=13, Fedora >=40; wymagane GTK >=4.12 i Qt >=6.4.')

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('action', choices=['check', 'install', 'plan'], default='check', nargs='?'); args = parser.parse_args()
    needed = missing()
    if not needed:
        print('Zależności dostępne: GTK >=4.12, Qt >=6.4 (silnik), OpenGL/EGL, libvips, LibRaw, zlib i narzędzia instalacji.')
        cuda = shutil.which('nvcc') or next((str(p) for p in [Path('/opt/cuda/bin/nvcc'), Path('/usr/local/cuda/bin/nvcc')] if p.is_file()), None)
        print('nvJPEG: SDK CUDA znaleziony: '+cuda if cuda else 'nvJPEG opcjonalne: wymagany CUDA Toolkit >=12 z nvcc i nvJPEG; CPU działa bez CUDA.')
        return
    command = plan()
    import shlex
    print('Brakuje: ' + ', '.join(needed))
    print('Pakiety dystrybucji: ' + shlex.join(['sudo', *command]))
    if args.action == 'plan': return
    if args.action != 'install': sys.exit(2)
    prefix = [] if os.geteuid() == 0 else ['sudo']
    if command[0] == 'apt-get': subprocess.run([*prefix, 'apt-get', 'update'], check=True)
    # No shell, global GPU configuration, external repos or pip/curl installers.
    subprocess.run([*prefix, *command], check=True)
    remaining = missing()
    if remaining: raise RuntimeError('Po instalacji nadal brakuje: ' + ', '.join(remaining))

if __name__ == '__main__':
    try: main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc: sys.exit(str(exc))
