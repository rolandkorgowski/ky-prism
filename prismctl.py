#!/usr/bin/env python3
"""ky-prism: kopie zapasowe i paczki do przenoszenia.

  backup [--ustawienia|--program]       kopia do ./backups/ (bez opcji: wybór w terminalu)
  restore PLIK                          ustawienia z kopii, z kopii programu albo z paczki export
  export [--system|--konto] [--bez-ustawien]
                                        paczka do ./dist/ (bez opcji: wybór w terminalu)
  import [PLIK]                         ustawienia z rozpakowanej paczki
  z-paczki                              pierwsze uruchomienie rozpakowanej paczki
  prebuilt-ok                           czy gotowy program z paczki pasuje do tego systemu

Ustawienia to: ulubione, zakładki katalogów, język i krycie interfejsu. Kopie
nigdy nie zawierają zdjęć ani cache miniatur. Pliki z ustawieniami mają prawa 600.
"""
import argparse
import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
import package  # noqa: E402  (SOURCES, digest, nvjpeg artifacts)

SETTINGS_ARCHIVE = 'ky-prism-ustawienia.tar.gz'
SETTINGS_DIR = 'ky-prism-ustawienia'
FIRST_RUN_MARK = '.ky-prism-z-paczki'


def say(text):
    print('\033[1;32m[ky-prism]\033[0m ' + text)


def warn(text):
    print('\033[1;33m[ky-prism]\033[0m ' + text, file=sys.stderr)


def version():
    match = re.search(r'project\(KyPrism VERSION ([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text())
    return match.group(1) if match else '0'


def stamp():
    return datetime.datetime.now().strftime('%Y%m%d-%H%M%S')


# ---------------------------------------------------------------- settings ---
def data_home():
    return Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')


def config_home():
    return Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config')


def _favorites(data):
    value = json.loads(data)
    if not isinstance(value, dict) or value.get('version') != 1 or not isinstance(value.get('photos'), list):
        raise ValueError('nieprawidłowy format ulubionych')
    return data


def _bookmarks(data):
    value = json.loads(data)
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ValueError('nieprawidłowy format zakładek')
    return data


def _language(data):
    value = json.loads(data)
    if not isinstance(value, dict) or value.get('version') != 1 or value.get('language') not in ('pl', 'en'):
        raise ValueError('nieprawidłowy plik języka')
    return (json.dumps({'version': 1, 'language': value['language']}) + '\n').encode()


def _appearance(data):
    value = json.loads(data)
    percent = value.get('ui_opacity_percent') if isinstance(value, dict) else None
    if type(percent) is not int or not 0 <= percent <= 100:
        raise ValueError('nieprawidłowe krycie interfejsu')
    return (json.dumps({'ui_opacity_percent': percent}) + '\n').encode()


# (name in the archive, description, location, validator, size limit)
SETTINGS = [
    ('data/favorites.json', 'ulubione zdjęcia', lambda: data_home() / 'omarchy-prism/favorites.json', _favorites, 8 << 20),
    ('data/bookmarks.json', 'zakładki katalogów', lambda: data_home() / 'omarchy-prism/bookmarks.json', _bookmarks, 128 << 10),
    ('data/language.json', 'język interfejsu', lambda: data_home() / 'omarchy-prism/language.json', _language, 4 << 10),
    ('config/appearance.json', 'krycie interfejsu', lambda: config_home() / 'ky-prism/appearance.json', _appearance, 4 << 10),
]
# Older exports kept the same files in ky-prism/user-data/.
LEGACY_NAMES = {'favorites.json': 'data/favorites.json', 'bookmarks.json': 'data/bookmarks.json',
                'language.json': 'data/language.json', 'appearance.json': 'config/appearance.json'}


def no_links(path):
    """Refuse a target that is, or lies under, a symbolic link."""
    for item in (path, *path.parents):
        if item.is_symlink():
            raise ValueError(f'Dowiązanie symboliczne w ścieżce ustawień: {item}')


def current_settings():
    """Valid settings of this account as {archive name: bytes}."""
    found = {}
    for name, _, where, check, limit in SETTINGS:
        path = where()
        if not path.is_file():
            continue
        no_links(path)
        if path.stat().st_size > limit:
            raise ValueError(f'{path} przekracza limit {limit} B')
        try:
            found[name] = check(path.read_bytes())
        except ValueError as error:
            warn(f'Pominięto {path}: {error}')
    return found


def settings_tar(files, out):
    """Write a settings archive (mode 600) with a short description."""
    described = {name: text for name, text, *_ in SETTINGS}
    listing = '\n'.join(f'  {name}: {described[name]}' for name in files)
    readme = (f'ky-prism {version()} — ustawienia\nutworzono: {datetime.datetime.now():%Y-%m-%d %H:%M:%S}\n'
              f'zawartość:\n{listing}\nBez zdjęć i bez cache miniatur.\n'
              'Przywrócenie: ./run.sh restore ' + out.name + '\n').encode()
    out.parent.mkdir(parents=True, exist_ok=True)
    old = os.umask(0o077)
    try:
        with tarfile.open(out, 'w:gz') as tar:
            for name, data in [*files.items(), ('USTAWIENIA.txt', readme)]:
                info = tarfile.TarInfo(f'{SETTINGS_DIR}/{name}')
                info.size, info.mode, info.mtime = len(data), 0o600, int(datetime.datetime.now().timestamp())
                tar.addfile(info, io.BytesIO(data))
    finally:
        os.umask(old)
    os.chmod(out, 0o600)


def describe(files):
    described = {name: text for name, text, *_ in SETTINGS}
    return ', '.join(described[name] for name in files) or 'brak'


def read_member(tar, member, limit):
    if not member.isfile() or member.size > limit:
        raise ValueError(f'Nieprawidłowy wpis w kopii: {member.name}')
    return tar.extractfile(member).read()


def settings_from(path, depth=0):
    """Settings stored in a backup, a program backup or an export package."""
    allowed = {name: (check, limit) for name, _, _, check, limit in SETTINGS}
    found = {}
    with tarfile.open(path) as tar:
        for member in tar.getmembers():
            parts = Path(member.name).parts
            if '..' in parts or member.name.startswith('/'):
                raise ValueError(f'Niebezpieczna ścieżka w archiwum: {member.name}')
            name = None
            if len(parts) >= 2 and parts[0] == SETTINGS_DIR:
                name = '/'.join(parts[1:])
            elif len(parts) == 3 and parts[1] == 'user-data' and parts[2] in LEGACY_NAMES:
                name = LEGACY_NAMES[parts[2]]
            elif depth == 0 and len(parts) == 3 and parts[1] == 'ustawienia' and parts[2] == SETTINGS_ARCHIVE:
                inner = read_member(tar, member, 16 << 20)
                with tempfile.TemporaryDirectory(prefix='ky-prism-') as tmp:
                    nested = Path(tmp) / SETTINGS_ARCHIVE
                    nested.write_bytes(inner)
                    found.update(settings_from(nested, depth + 1))
                continue
            if name in allowed:
                check, limit = allowed[name]
                found[name] = check(read_member(tar, member, limit))
    return found


def write_settings(files):
    locations = {name: where for name, _, where, *_ in SETTINGS}
    for name, data in files.items():
        target = locations[name]()
        no_links(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        fd, tmp = tempfile.mkstemp(prefix='.ky-prism-', dir=target.parent)
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            os.chmod(tmp, 0o600)
            os.replace(tmp, target)
        finally:
            Path(tmp).unlink(missing_ok=True)


def unique(path):
    """Never overwrite an earlier backup made in the same second."""
    candidate, n = path, 1
    while candidate.exists():
        candidate = path.with_name(path.name.replace('.tar.gz', f'-{n}.tar.gz'))
        n += 1
    return candidate


def backups_dir(override=None):
    override = override or os.environ.get('PRISM_BACKUP_DIR')
    folder = Path(override).resolve() if override else ROOT / 'backups'
    folder.mkdir(parents=True, exist_ok=True)
    if not override:
        os.chmod(folder, 0o700)
    return folder


def restore(path):
    path = Path(path).resolve()
    if not path.is_file():
        raise ValueError(f'Brak pliku: {path}')
    files = settings_from(path)
    if not files:
        raise ValueError('W tym pliku nie ma ustawień ky-prism.')
    existing = current_settings()
    if existing:
        safety = unique(backups_dir() / f'ky-prism-ustawienia-przed-przywroceniem-{stamp()}.tar.gz')
        settings_tar(existing, safety)
        say(f'Obecne ustawienia zapisano wcześniej w: {safety}')
    write_settings(files)
    say(f'Przywrócono: {describe(files)}')
    print('  Uruchom ponownie ky-prism, jeśli jest otwarty.')


# ---------------------------------------------------------------- packages ---
def os_identity():
    system = platform.system()
    info = {'kernel': system, 'machine': platform.machine(), 'release': platform.release()}
    if system == 'Linux':
        values = {}
        try:
            for line in Path('/etc/os-release').read_text().splitlines():
                if '=' in line:
                    k, v = line.split('=', 1)
                    values[k] = v.strip('"')
        except OSError:
            pass
        info.update(id=values.get('ID', ''), version_id=values.get('VERSION_ID', ''),
                    name=values.get('PRETTY_NAME', 'Linux'))
    else:
        info.update(id=system.lower(), version_id=platform.release(), name=f'{system} {platform.release()}')
    return info


def system_name():
    return os_identity()['name'] + ' ' + platform.machine()


def install_text(archive_name, folder, prebuilt):
    """INSTALACJA.txt placed in every package."""
    first = ('Gotowy program w prebuilt/ pasuje tylko do systemu z KOPIA.txt; na innym systemie\n'
             './run.sh skompiluje ky-prism na miejscu.\n') if prebuilt else (
             'Program kompiluje się na miejscu (kilka minut); potrzebne pakiety instalator\n'
             'pobiera z oficjalnych repozytoriów systemu, pytając o hasło (sudo/doas).\n')
    return f"""ky-prism {version()} — instalacja z paczki
{'=' * 44}

{first}
1. Przygotuj system (raz; tylko Python 3 — resztę dobierze instalator):

   Ubuntu 24.04 / 26.04, Debian, Linux Mint:
       sudo apt install python3
   Arch Linux, Omarchy, Manjaro, EndeavourOS:
       sudo pacman -S --needed python
   Fedora:
       sudo dnf install python3
   FreeBSD 14 / 15 (jako root):
       pkg install -y python3 sudo
       visudo            # odkomentuj: %wheel ALL=(ALL:ALL) ALL; konto w grupie wheel
   OpenBSD 7.6+ (jako root):
       pkg_add python%3
       echo 'permit persist :wheel' > /etc/doas.conf

2. Rozpakuj i zainstaluj (jako zwykły użytkownik):

       tar xzf {archive_name}
       cd {folder}
       ./run.sh

   ./run.sh w rozpakowanej paczce pyta przed zmianami, potem przywraca ustawienia
   z paczki i instaluje ky-prism do ~/.local (program, menu aplikacji, ikona).
   Po kolei ręcznie:  ./run.sh import   (ustawienia)
                      ./run.sh install  (zależności, kompilacja, testy, instalacja)

3. Uruchom: ky-prism z menu aplikacji albo w terminalu:  ky-prism [zdjęcie]
   Jeśli polecenia nie ma w PATH (częste na FreeBSD/OpenBSD), dopisz do ~/.profile:
       export PATH="$HOME/.local/bin:$PATH"
   i zaloguj się ponownie.

Historia git (jeśli jest w paczce):  git clone ky-prism.git.bundle ky-prism
Szczegóły dla każdego systemu: docs/INSTALACJA.md
"""


def copy_sources(stage):
    for name in package.SOURCES + ['MANIFEST.json', '.gitignore']:
        src = ROOT / name
        if not src.exists():
            continue
        dst = stage / name
        if src.is_dir():
            shutil.copytree(src, dst, ignore=shutil.ignore_patterns('__pycache__'), symlinks=False)
        else:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)


def git_bundle(stage):
    if not shutil.which('git') or not (ROOT / '.git').exists():
        return False
    result = subprocess.run(['git', '-C', str(ROOT), 'bundle', 'create', str(stage / 'ky-prism.git.bundle'), '--all'],
                            capture_output=True)
    if result.returncode:
        warn('Nie udało się spakować historii git — paczka bez niej.')
    return result.returncode == 0


def ensure_built():
    if (ROOT / 'build/ky-prism').is_file():
        return
    say('Brak zbudowanego programu — kompilacja (./run.sh build)')
    subprocess.run(['sh', str(ROOT / 'run.sh'), 'build'], check=True)


def add_prebuilt(stage):
    target = stage / 'prebuilt'
    target.mkdir()
    shutil.copy2(ROOT / 'build/ky-prism', target / 'ky-prism')
    module = package.nvjpeg_artifact()
    if module:
        shutil.copy2(module, target / 'libprism_nvjpeg.so')
    metadata = package.nvjpeg_metadata()
    if metadata:
        shutil.copy2(metadata, target / 'nvjpeg-build.json')
    (target / 'SYSTEM.json').write_text(json.dumps(os_identity(), indent=2) + '\n')


def build_package(out, kind, settings, binary):
    """Sources (+ git history, + prebuilt program, + settings) as one .tar.gz."""
    folder = f'ky-prism-{version()}'
    with tempfile.TemporaryDirectory(prefix='ky-prism-paczka-') as tmp:
        stage = Path(tmp) / folder
        stage.mkdir()
        os.chmod(tmp, 0o700)
        say(f'Kopiowanie źródeł ({folder})')
        copy_sources(stage)
        # Checksums of exactly what is packed: ./run.sh verifies them first.
        package.write_manifest(stage)
        with_git = git_bundle(stage)
        if with_git:
            say('Dołączona historia git (ky-prism.git.bundle)')
        if binary:
            add_prebuilt(stage)
            say(f'Dołączony gotowy program ({system_name()})')
        if settings:
            (stage / 'ustawienia').mkdir(mode=0o700)
            settings_tar(settings, stage / 'ustawienia' / SETTINGS_ARCHIVE)
            say(f'Dołączone ustawienia: {describe(settings)}')
        (stage / 'KOPIA.txt').write_text(
            f'ky-prism {version()}\nrodzaj: {kind}\nutworzono: {datetime.datetime.now():%Y-%m-%d %H:%M:%S}\n'
            f'system: {system_name()}\n'
            f"program: {'prebuilt/ky-prism (gotowy dla tego systemu; na innym ./run.sh kompiluje)' if binary else 'do zbudowania: ./run.sh'}\n"
            f"ustawienia: {('ustawienia/' + SETTINGS_ARCHIVE + ' — ' + describe(settings)) if settings else 'brak'}\n"
            f"historia git: {'ky-prism.git.bundle' if with_git else 'brak'}\n"
            'bez zdjęć i bez cache miniatur\n')
        (stage / 'INSTALACJA.txt').write_text(install_text(out.name, folder, binary))
        out.parent.mkdir(parents=True, exist_ok=True)
        partial = out.with_name(out.name + '.part')
        old = os.umask(0o077)
        try:
            with tarfile.open(partial, 'w:gz') as tar:
                tar.add(stage, arcname=folder)
        finally:
            os.umask(old)
        os.replace(partial, out)
    os.chmod(out, 0o600 if settings else 0o644)
    out.with_name(out.name + '.sha256').write_text(package.digest(out) + '  ' + out.name + '\n')
    size = out.stat().st_size / 1048576
    print(f'  \033[32m✔\033[0m Paczka: {out} ({size:.1f} MiB)')
    return folder


def choose(question, first, second):
    if not (sys.stdin.isatty() and sys.stderr.isatty()):
        return 1
    print(f'{question}\n  1) {first}\n  2) {second}', file=sys.stderr)
    print('Wybór [1/2, Enter = 1]: ', end='', file=sys.stderr, flush=True)
    answer = sys.stdin.readline().strip()
    return 2 if answer == '2' else 1


def cmd_backup(args):
    what = args.what or ('program' if choose(
        'Co skopiować?',
        'ustawienia (ulubione, zakładki, język, krycie interfejsu)',
        'cały program: źródła, historia git, zbudowany program + ustawienia') == 2 else 'settings')
    folder = backups_dir(args.katalog)
    if what == 'settings':
        files = current_settings()
        if not files:
            raise ValueError('Brak ustawień ky-prism na tym koncie (nic do skopiowania).')
        out = unique(folder / f'ky-prism-ustawienia-{stamp()}.tar.gz')
        settings_tar(files, out)
        out.with_name(out.name + '.sha256').write_text(package.digest(out) + '  ' + out.name + '\n')
        print(f'  \033[32m✔\033[0m Kopia ustawień: {out}')
        print(f'  Zawartość: {describe(files)}')
        print(f'  Przywrócenie:  ./run.sh restore {out}')
        return
    binary = (ROOT / 'build/ky-prism').is_file()
    if not binary:
        warn('Brak zbudowanego programu (./run.sh build) — kopia bez gotowego programu.')
    out = unique(folder / f'ky-prism-{version()}-kopia-programu-{stamp()}.tar.gz')
    name = build_package(out, 'kopia programu (ten komputer, to konto)', current_settings(), binary)
    print(f'  Przywrócenie ustawień:  ./run.sh restore {out}')
    print(f'  Przywrócenie programu:  tar xzf {out.name} && cd {name} && ./run.sh')
    print(f'  Historia git:           git clone {name}/ky-prism.git.bundle ky-prism')


def cmd_export(args):
    target = args.target or ('account' if choose(
        'Dokąd przenieść ky-prism?',
        'inny komputer albo system (źródła, kompilacja na miejscu — każdy obsługiwany system)',
        'inne konto na tym komputerze (także gotowy program — bez kompilacji)') == 2 else 'system')
    settings = {} if args.no_settings else current_settings()
    folder = Path(args.katalog).resolve() if args.katalog else ROOT / 'dist'
    if target == 'account':
        ensure_built()
        out = folder / f'ky-prism-{version()}-na-konto.tar.gz'
        name = build_package(out, f'przeniesienie na inne konto ({system_name()})', settings, True)
    else:
        out = folder / f'ky-prism-{version()}-przeniesienie.tar.gz'
        name = build_package(out, 'przeniesienie na inny komputer lub system', settings, False)
    print()
    if target == 'account':
        print('  Na drugim koncie (ten sam komputer — bez kompilacji i bez sieci):')
        print(f'    cp {out} /tmp/ && chmod 644 /tmp/{out.name}   # albo inna droga, np. scp')
        print(f'    tar xzf {out.name} && cd {name}')
        print('    ./run.sh             # ustawienia z paczki + instalacja (pyta przed zmianami)')
        if settings:
            print('  Plik zawiera ustawienia (prawa 600) — usuń kopię z /tmp po przeniesieniu.')
    else:
        print('  Na nowym komputerze (Ubuntu 24.04/26.04, Debian, Fedora, Arch/Omarchy, FreeBSD, OpenBSD):')
        print(f'    tar xzf {out.name}')
        print(f'    cd {name}')
        print('    ./run.sh             # ustawienia z paczki, zależności, kompilacja, instalacja — pyta przed zmianami')
        print('  Wymagany tylko Python 3; instrukcja dla każdego systemu: INSTALACJA.txt w paczce.')
    print('  Po kolei ręcznie: ./run.sh import (ustawienia), ./run.sh install (instalacja)')


def bundled_settings():
    return ROOT / 'ustawienia' / SETTINGS_ARCHIVE


def cmd_import(args):
    path = Path(args.file) if args.file else bundled_settings()
    if not path.is_file():
        raise ValueError('Brak ustawień w paczce (ustawienia/' + SETTINGS_ARCHIVE + '). Podaj plik: ./run.sh import PLIK')
    restore(path)


def prebuilt_ok(verbose=False):
    binary = ROOT / 'prebuilt/ky-prism'
    meta = ROOT / 'prebuilt/SYSTEM.json'
    if not binary.is_file() or not meta.is_file():
        return False
    built = json.loads(meta.read_text())
    here = os_identity()
    for key in ('kernel', 'machine', 'id', 'version_id'):
        if built.get(key) != here.get(key):
            if verbose:
                say(f"Gotowy program jest dla {built.get('name')} {built.get('machine')}; tu będzie kompilacja.")
            return False
    ldd = shutil.which('ldd')
    if ldd:
        result = subprocess.run([ldd, str(binary)], capture_output=True, text=True)
        if result.returncode or 'not found' in result.stdout:
            if verbose:
                say('Gotowemu programowi brakuje bibliotek systemu; tu będzie kompilacja.')
            return False
    return True


def cmd_first_run(args):
    info = (ROOT / 'KOPIA.txt').read_text()
    print(info)
    settings = bundled_settings().is_file()
    plan = ['ustawienia z paczki zastąpią obecne (wcześniej kopia do backups/)'] if settings else []
    plan.append('instalacja ky-prism do ~/.local' + (' (gotowy program)' if prebuilt_ok(True) else
                                                     ' (zależności z repozytoriów systemu, kompilacja, testy)'))
    print('Pierwsze uruchomienie paczki:\n  - ' + '\n  - '.join(plan))
    if not sys.stdin.isatty():
        print('Bez terminala niczego nie zmieniam. Ręcznie: ./run.sh import && ./run.sh install')
        return
    print('Kontynuować? [T/n]: ', end='', flush=True)
    if sys.stdin.readline().strip().lower() in ('n', 'nie', 'no'):
        print('Pominięto. Później: ./run.sh import && ./run.sh install')
        (ROOT / FIRST_RUN_MARK).write_text(f'pominięto {datetime.datetime.now():%Y-%m-%d %H:%M:%S}\n')
        return
    if settings:
        restore(bundled_settings())
    result = subprocess.run(['sh', str(ROOT / 'run.sh'), 'install'])
    if result.returncode:
        sys.exit(result.returncode)
    (ROOT / FIRST_RUN_MARK).write_text(f'zainstalowano {datetime.datetime.now():%Y-%m-%d %H:%M:%S}\n')


def main():
    parser = argparse.ArgumentParser(prog='./run.sh', description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='action', required=True)
    p = sub.add_parser('backup')
    p.add_argument('--ustawienia', '--settings', dest='what', action='store_const', const='settings')
    p.add_argument('--program', '--full', dest='what', action='store_const', const='program')
    p.add_argument('--katalog', help=argparse.SUPPRESS)
    p.set_defaults(run=cmd_backup)
    p = sub.add_parser('restore')
    p.add_argument('file')
    p.set_defaults(run=lambda a: restore(a.file))
    p = sub.add_parser('export')
    p.add_argument('--system', '--komputer', dest='target', action='store_const', const='system')
    p.add_argument('--konto', '--account', dest='target', action='store_const', const='account')
    p.add_argument('--bez-ustawien', '--bez-ustawień', '--no-settings', dest='no_settings', action='store_true')
    p.add_argument('--katalog', help=argparse.SUPPRESS)
    p.set_defaults(run=cmd_export)
    p = sub.add_parser('import')
    p.add_argument('file', nargs='?')
    p.set_defaults(run=cmd_import)
    p = sub.add_parser('z-paczki')
    p.set_defaults(run=cmd_first_run)
    p = sub.add_parser('prebuilt-ok')
    p.set_defaults(run=lambda a: sys.exit(0 if prebuilt_ok() else 1))
    args = parser.parse_args()
    args.run(args)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, tarfile.TarError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print('\033[1;31m[ky-prism]\033[0m ' + str(error), file=sys.stderr)
        sys.exit(1)
