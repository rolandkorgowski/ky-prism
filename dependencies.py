#!/usr/bin/env python3
"""Dependency preflight. Installs only missing requirements using system packages.

Supported: Ubuntu >= 24.04, Debian >= 13, Fedora >= 40, Arch/Omarchy (and
Arch-based), FreeBSD >= 14, OpenBSD >= 7.6. Requires GTK >= 4.12 and Qt >= 6.4.
"""
import argparse
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys

# The window is GTK 4; the decoder/cache engine uses the non-visual Qt modules.
# Qt Widgets/Test are optional: only the legacy front end and its tests use them.
MODULES = ['gtk4', 'epoxy', 'Qt6Core', 'Qt6Gui', 'Qt6Concurrent', 'vips-cpp', 'libraw', 'egl', 'gl', 'zlib']

# System package names per family. FreeBSD/OpenBSD names are the ports'
# package names (pkg / pkg_add); pkg-config, make and the C++ compiler are in
# the BSD base system.
PACKAGES = {
    'arch': ['base-devel', 'cmake', 'pkgconf', 'python', 'qt6-base', 'gtk4', 'libepoxy', 'libvips', 'libraw',
             'libglvnd', 'libjpeg-turbo', 'lcms2', 'zlib', 'util-linux', 'udisks2', 'desktop-file-utils', 'xdg-utils'],
    'debian': ['build-essential', 'cmake', 'pkg-config', 'python3', 'qt6-base-dev', 'qt6-base-dev-tools',
               'libqt6opengl6-dev', 'libgtk-4-dev', 'libepoxy-dev', 'libvips-dev', 'libraw-dev', 'liblcms2-dev',
               'libjpeg-dev', 'zlib1g-dev', 'libglvnd-dev', 'libegl-dev', 'libgl-dev', 'util-linux', 'udisks2',
               'desktop-file-utils', 'xdg-utils'],
    'fedora': ['gcc-c++', 'make', 'cmake', 'pkgconf-pkg-config', 'python3', 'qt6-qtbase-devel', 'gtk4-devel',
               'libepoxy-devel', 'vips-devel', 'LibRaw-devel', 'lcms2-devel', 'libjpeg-turbo-devel', 'zlib-devel',
               'libglvnd-devel', 'mesa-libEGL-devel', 'mesa-libGL-devel', 'util-linux', 'udisks2',
               'desktop-file-utils', 'xdg-utils'],
    'freebsd': ['cmake', 'pkgconf', 'python3', 'qt6-base', 'gtk4', 'libepoxy', 'vips', 'libraw', 'lcms2',
                'jpeg-turbo', 'libglvnd', 'mesa-libs', 'desktop-file-utils', 'xdg-utils'],
    # OpenBSD: libepoxy, EGL and GL come with the X11 base sets (xbase).
    'openbsd': ['cmake', 'python%3', 'qt6-qtbase', 'gtk+4', 'libvips', 'libraw', 'lcms2', 'jpeg',
                'desktop-file-utils', 'xdg-utils'],
}


def family():
    system = platform.system()
    if system == 'FreeBSD':
        return 'freebsd'
    if system == 'OpenBSD':
        return 'openbsd'
    values = {}
    for line in Path('/etc/os-release').read_text().splitlines():
        if '=' in line:
            k, v = line.split('=', 1)
            values[k] = v.strip('"')
    ids = {values.get('ID', ''), *values.get('ID_LIKE', '').split()}
    if ids & {'arch', 'omarchy'}:
        return 'arch'
    if ids & {'debian', 'ubuntu'}:
        return 'debian'
    if ids & {'fedora'}:
        return 'fedora'
    raise RuntimeError('Obsługiwane: Ubuntu >=24.04, Debian >=13, Fedora >=40, Arch/Omarchy, FreeBSD >=14, '
                       'OpenBSD >=7.6; wymagane GTK >=4.12 i Qt >=6.4.')


def tools(kind):
    """Commands the build and the installer need on this system."""
    compiler = 'c++' if kind in ('freebsd', 'openbsd') else 'g++'
    result = ['cmake', 'pkg-config', compiler, 'make', 'python3']
    if kind not in ('freebsd', 'openbsd'):
        # Pendrive support (Linux): block devices and mounting without root.
        result += ['lsblk', 'udisksctl']
    result.append('update-desktop-database')
    return result


def missing(kind=None):
    kind = kind or family()
    result = [name for name in tools(kind) if not shutil.which(name)]
    if shutil.which('pkg-config'):
        result += [name for name in MODULES
                   if subprocess.run(['pkg-config', '--exists', name], capture_output=True).returncode]
        if subprocess.run(['pkg-config', '--atleast-version=6.4', 'Qt6Core'], capture_output=True).returncode:
            result.append('Qt >= 6.4')
        if subprocess.run(['pkg-config', '--atleast-version=4.12', 'gtk4'], capture_output=True).returncode:
            result.append('GTK >= 4.12')
    else:
        result += MODULES
    return list(dict.fromkeys(result))


def plan(kind=None):
    kind = kind or family()
    names = PACKAGES[kind]
    if kind == 'arch':
        return ['pacman', '-S', '--needed', *names]
    if kind == 'debian':
        return ['apt-get', 'install', '--no-install-recommends', *names]
    if kind == 'fedora':
        return ['dnf', 'install', '--setopt=install_weak_deps=False', *names]
    if kind == 'freebsd':
        return ['pkg', 'install', *names]
    return ['pkg_add', *names]


def privilege():
    """sudo on Linux; doas or sudo on BSD; nothing for root."""
    if os.geteuid() == 0:
        return []
    for tool in ('sudo', 'doas') if platform.system() == 'Linux' else ('doas', 'sudo'):
        if shutil.which(tool):
            return [tool]
    raise RuntimeError('Brak sudo/doas. Uruchom jako root: ' + shlex.join(plan()) +
                       ' (FreeBSD: su -m root -c "…", OpenBSD: doas wymaga /etc/doas.conf).')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['check', 'install', 'plan'], default='check', nargs='?')
    args = parser.parse_args()
    kind = family()
    needed = missing(kind)
    if not needed:
        print('Zależności dostępne: GTK >=4.12, Qt >=6.4 (silnik), OpenGL/EGL, libvips, LibRaw, zlib i narzędzia instalacji.')
        if kind not in ('freebsd', 'openbsd'):
            cuda = shutil.which('nvcc') or next((str(p) for p in [Path('/opt/cuda/bin/nvcc'), Path('/usr/local/cuda/bin/nvcc')] if p.is_file()), None)
            print('nvJPEG: SDK CUDA znaleziony: ' + cuda if cuda else 'nvJPEG opcjonalne: wymagany CUDA Toolkit >=12 z nvcc i nvJPEG; CPU działa bez CUDA.')
        return
    command = plan(kind)
    print('Brakuje: ' + ', '.join(needed))
    shown = (['sudo'] if kind not in ('freebsd', 'openbsd') else ['doas']) if os.geteuid() else []
    print('Pakiety systemu: ' + shlex.join([*shown, *command]))
    if args.action == 'plan':
        return
    if args.action != 'install':
        sys.exit(2)
    prefix = privilege()
    if command[0] == 'apt-get':
        subprocess.run([*prefix, 'apt-get', 'update'], check=True)
    if not sys.stdin.isatty():
        # Without a terminal the package manager cannot ask: confirm in advance.
        flag = {'apt-get': '-y', 'dnf': '-y', 'pkg': '-y', 'pacman': '--noconfirm', 'pkg_add': '-I'}[command[0]]
        command.insert(1 if command[0] == 'pkg_add' else 2, flag)
    # No shell, global GPU configuration, external repos or pip/curl installers.
    subprocess.run([*prefix, *command], check=True)
    remaining = missing(kind)
    if remaining:
        raise RuntimeError('Po instalacji nadal brakuje: ' + ', '.join(remaining))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        sys.exit(str(exc))
