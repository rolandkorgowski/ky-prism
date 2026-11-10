#!/usr/bin/env python3
"""User-owned installation, reversible backup and checksummed offline source bundle."""
import configparser
import datetime, hashlib, json, os, shutil, subprocess, sys, tarfile, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent
SOURCES = ['CMakeLists.txt', 'dependencies.py', 'run.sh', 'package.py', 'ky-prism', 'ky-prism.desktop', 'COPYRIGHT.md', 'KEYBOARD.md', 'omarchy-prism', 'omarchy-prism.desktop', 'omarchy-prism.svg', 'README.md', 'LICENSE', 'prism.lua', 'src', 'tests', 'plugin', 'terminal-plugin', 'chrome-plugin', 'omarchy-prism-chrome', 'omarchy-prism-terminal', 'omarchy-prism-open', 'publication/prepare.py', 'publication/README.md', 'publication/templates', 'publication/tests', 'i18n', 'diagnose-hang.py', 'transfer.py', 'updates', 'docs']
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024*1024), b''): h.update(block)
    return h.hexdigest()
def nvjpeg_artifact():
    metadata=ROOT/'build/nvjpeg-build.json'
    if metadata.is_file():
        if not json.loads(metadata.read_text()).get('enabled'): return None
        module=ROOT/'build/libprism_nvjpeg.so'
        if not module.is_file(): raise ValueError('Brakuje zbudowanego modułu nvJPEG')
        return module
    module=ROOT/'prebuilt/libprism_nvjpeg.so'
    return module if module.is_file() else None
def nvjpeg_metadata():
    for name in ('build/nvjpeg-build.json','prebuilt/nvjpeg-build.json'):
        path=ROOT/name
        if path.is_file(): return path
    return None
def mappings(home):
    share = home / '.local/share/omarchy-prism'
    result = [(ROOT/'build/ky-prism',share/'bin/ky-prism'),
              (ROOT/'build/ky-prism',share/'bin/omarchy-prism'),
              (ROOT/'ky-prism',home/'.local/bin/ky-prism'),
              (ROOT/'omarchy-prism',home/'.local/bin/omarchy-prism'),
              (ROOT/'ky-prism.desktop',home/'.local/share/applications/ky-prism.desktop'),
              (ROOT/'omarchy-prism.desktop',home/'.local/share/applications/omarchy-prism.desktop'),
              (ROOT/'omarchy-prism.svg',home/'.local/share/icons/hicolor/scalable/apps/ky-prism.svg'),
              (ROOT/'omarchy-prism.svg',home/'.local/share/icons/hicolor/scalable/apps/omarchy-prism.svg'),
              (ROOT/'prism.lua',home/'.config/hypr/prism.lua')]
    module=nvjpeg_artifact()
    if module: result.append((module,share/'lib/libprism_nvjpeg.so'))
    metadata=nvjpeg_metadata()
    if metadata: result.append((metadata,share/'lib/nvjpeg-build.json'))
    result.append((ROOT/'omarchy-prism-open',home/'.local/bin/omarchy-prism-open'))
    result.append((ROOT/'omarchy-prism-chrome',home/'.local/bin/omarchy-prism-chrome'))
    result.extend((p,home/'.config/omarchy/plugins/kylen.prism-chrome'/p.name) for p in sorted((ROOT/'chrome-plugin').iterdir()) if p.is_file())
    result.append((ROOT/'omarchy-prism-terminal',home/'.local/bin/omarchy-prism-terminal'))
    result.extend((p,home/'.config/omarchy/plugins/kylen.prism-terminal'/p.name) for p in sorted((ROOT/'terminal-plugin').iterdir()) if p.is_file())
    result.extend((p,home/'.config/omarchy/plugins/kylen.prism'/p.name) for p in sorted((ROOT/'plugin').iterdir()) if p.is_file())
    for name in SOURCES:
        p=ROOT/name
        for f in sorted(p.rglob('*')) if p.is_dir() else [p]:
            if f.is_file() and '__pycache__' not in f.parts: result.append((f,share/'source'/f.relative_to(ROOT)))
    if (ROOT/"MANIFEST.json").exists(): result.append((ROOT/"MANIFEST.json",share/"source/MANIFEST.json"))
    if not (home/".config/omarchy").is_dir() and not Path("/usr/share/omarchy").is_dir():
        result=[(src,dst) for src,dst in result if not dst.is_relative_to(home/".config/omarchy/plugins") and dst!=home/".config/hypr/prism.lua"]
    return result

def shell_layout(data):
    data=json.loads(json.dumps(data))
    sections=data['bar']['layout']
    previous={}
    for rows in sections.values():
        for row in rows:
            if row.get('id') in ('kylen.prism','kylen.prism-terminal','kylen.prism-chrome'): previous[row['id']]=row
    for key,rows in sections.items(): sections[key]=[r for r in rows if r.get('id') not in ('kylen.prism','kylen.prism-terminal','kylen.prism-chrome')]
    left=sections['left']
    # Keep launchers together: menu, Files, Prism, Alacritty, Chrome. Media
    # widgets may have been inserted before Files by an older bar migration.
    dolphin=next((r for r in left if r.get('id')=='kylen.files-dolphin'),None)
    menu=next((i for i,r in enumerate(left) if r.get('id') in ('kylen.menu','omarchy.menu')),None)
    if dolphin is not None and menu is not None:
        left.remove(dolphin)
        menu=next(i for i,r in enumerate(left) if r.get('id') in ('kylen.menu','omarchy.menu'))
        left.insert(menu+1,dolphin)
    position=next((i+1 for i,r in enumerate(left) if r.get('id')=='kylen.files-dolphin'),None)
    if position is None: position=next((i+1 for i,r in enumerate(left) if r.get('id') in ('kylen.menu','omarchy.menu')),0)
    left.insert(position,dict(previous.get('kylen.prism',{}),id='kylen.prism'))
    left.insert(position+1,dict(previous.get('kylen.prism-terminal',{}),id='kylen.prism-terminal'))
    left.insert(position+2,dict(previous.get('kylen.prism-chrome',{}),id='kylen.prism-chrome'))
    # Media measures the space AFTER workspaces. Keep the whole media group
    # there; placing it before workspaces makes its width feed back into its
    # own left boundary and causes repeated collapse/expansion.
    media_ids = ('kylen.media-glass','kylen.tidal-hires','kylen.cava-spectrum')
    if any(row.get('id') == 'omarchy.workspaces' for row in left):
        media = {row['id']:row for row in left if row.get('id') in media_ids}
        left[:] = [row for row in left if row.get('id') not in media_ids]
        index = next(i+1 for i,row in enumerate(left) if row.get('id') == 'omarchy.workspaces')
        left[index:index] = [media[key] for key in media_ids if key in media]
    return data

def configuration_errors(output):
    """Hyprland can encode no diagnostics as [""]. Fail closed on other schemas."""
    values=json.loads(output)
    if not isinstance(values,list) or any(not isinstance(value,str) for value in values):
        raise ValueError("Nieznany format odpowiedzi Hyprland configerrors")
    return [value.strip() for value in values if value.strip()]

def main():
    action=sys.argv[1]; home=Path.home()
    if action=='check-source':
        manifest=ROOT/'MANIFEST.json'
        if not manifest.is_file(): raise ValueError('Brak MANIFEST.json źródeł')
        for name,expected in json.loads(manifest.read_text()).items():
            p=ROOT/name
            if Path(name).is_absolute() or '..' in Path(name).parts or not p.resolve().is_relative_to(ROOT) or p.is_symlink(): raise ValueError('Nieprawidłowa ścieżka manifestu')
            if digest(p)!=expected: raise ValueError('Niezgodne źródła: '+name)
        print('Manifest źródeł poprawny.')
    elif action=='install':
        files=mappings(home)
        if not (ROOT/'build/ky-prism').is_file(): raise ValueError('Najpierw ./run.sh build')
        stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
        backup=home/'.local/state/omarchy-prism/install-backups'/stamp
        # Preflight every path, reject symlinks rather than following another installation.
        for _,dst in files:
            if any(p.is_symlink() for p in [dst,*dst.parents] if p!=home.parent): raise ValueError(f'Symlink: {dst}')
        backup.mkdir(parents=True,exist_ok=False)
        # Bundle manifests also list prebuilt/ and, for backups, installed/.
        # The recovery source tree has its own manifest of exactly the reviewed
        # source files copied here, never nonexistent archive-only objects.
        source_root=home/'.local/share/omarchy-prism/source'
        manifest={str(dst.relative_to(source_root)):digest(src) for src,dst in files
                  if dst.is_relative_to(source_root) and dst!=source_root/'MANIFEST.json'}
        staged_manifest=backup/'source-manifest.json'
        staged_manifest.write_text(json.dumps(manifest,indent=2)+'\n')
        files=[(src,dst) for src,dst in files if dst!=source_root/'MANIFEST.json']
        files.append((staged_manifest,source_root/'MANIFEST.json'))
        # User favorites are mutable data, never a checksummed installed artifact.
        favorite_file=home/'.local/share/omarchy-prism/favorites.json'
        bookmark_file=home/'.local/share/omarchy-prism/bookmarks.json'
        language_file=home/'.local/share/omarchy-prism/language.json'
        bundled=ROOT/'user-data/favorites.json'
        if bundled.is_file() and not favorite_file.exists():
            if favorite_file.is_symlink():raise ValueError('Dowiązanie w miejscu pliku ulubionych')
            if bundled.is_symlink() or bundled.stat().st_size>8*1024*1024:raise ValueError('Nieprawidłowa kopia ulubionych')
            files.append((bundled,favorite_file))
        bundled_bookmarks=ROOT/'user-data/bookmarks.json'
        if bundled_bookmarks.is_file() and not bookmark_file.exists():
            if bookmark_file.is_symlink() or bundled_bookmarks.is_symlink() or bundled_bookmarks.stat().st_size>128*1024: raise ValueError('Nieprawidłowa kopia zakładek')
            files.append((bundled_bookmarks,bookmark_file))
        bundled_language=ROOT/'user-data/language.json'
        if bundled_language.is_file() and not language_file.exists():
            if language_file.is_symlink() or bundled_language.is_symlink() or bundled_language.stat().st_size>4096:raise ValueError('Nieprawidłowa kopia języka')
            preference=json.loads(bundled_language.read_text())
            if preference.get('version')!=1 or preference.get('language') not in ('pl','en'):raise ValueError('Nieprawidłowy język')
            files.append((bundled_language,language_file))
        bundled_appearance=ROOT/'user-data/appearance.json'
        appearance_file=home/'.config/ky-prism/appearance.json'
        if bundled_appearance.is_file() and not appearance_file.exists():
            if bundled_appearance.is_symlink() or bundled_appearance.stat().st_size>4096: raise ValueError('Nieprawidłowe ustawienia wyglądu')
            preference=json.loads(bundled_appearance.read_text())
            percent=preference.get('ui_opacity_percent') if isinstance(preference,dict) else None
            if type(percent) is not int or not 0<=percent<=100: raise ValueError('Nieprawidłowe krycie interfejsu')
            if any(p.is_symlink() for p in (appearance_file,*appearance_file.parents)): raise ValueError('Dowiązanie w ustawieniach wyglądu')
            files.append((bundled_appearance,appearance_file))
        config=home/'.config/hypr/hyprland.lua'
        live_hypr=bool(os.environ.get('HYPRLAND_INSTANCE_SIGNATURE'))
        if config.exists():
            if config.is_symlink(): raise ValueError('hyprland.lua jest symlinkiem')
            text=config.read_text()
            if 'require("hypr.prism")' not in text:
                staged=backup/'hyprland-new.lua';staged.write_text(text+'\n-- Prism: fotografia bez globalnej przezroczystosci\nrequire("hypr.prism")\n');os.chmod(staged,config.stat().st_mode & 0o777);files.append((staged,config))
        # Register supported image types as defaults, as explicitly requested.
        desktop=backup/'ky-prism.desktop'
        text=(ROOT/'ky-prism.desktop').read_text()
        launcher=str(home/'.local/bin/ky-prism')
        escaped=launcher.replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$').replace('%','%%')
        text=text.replace('Exec=ky-prism %f','Exec="'+escaped+'" %f')
        desktop.write_text(text)
        files=[(desktop if dst==home/'.local/share/applications/ky-prism.desktop' else src,dst) for src,dst in files]
        legacy=backup/'legacy.desktop'
        legacy.write_text('\n'.join(line for line in text.splitlines() if not line.startswith('MimeType='))+'\nNoDisplay=true\n')
        files=[(legacy if dst==home/'.local/share/applications/omarchy-prism.desktop' else src,dst) for src,dst in files]
        mime=home/'.config/mimeapps.list'
        if mime.is_symlink() or mime.parent.is_symlink(): raise ValueError('Symlink mimeapps.list')
        parser=configparser.ConfigParser(interpolation=None,strict=False);parser.optionxform=str
        if mime.exists():parser.read(mime)
        if not parser.has_section('Added Associations'):parser.add_section('Added Associations')
        types=next(line.split('=',1)[1] for line in text.splitlines() if line.startswith('MimeType=')).strip(';').split(';')
        if not parser.has_section('Default Applications'):parser.add_section('Default Applications')
        for kind in types:
            parser.set('Default Applications',kind,'ky-prism.desktop;')
            entries=[entry for entry in parser.get('Added Associations',kind,fallback='').split(';') if entry and entry!='omarchy-prism.desktop']
            if 'ky-prism.desktop' not in entries:entries.append('ky-prism.desktop')
            parser.set('Added Associations',kind,';'.join(entries)+';')
            if parser.has_option('Removed Associations',kind):
                entries=[entry for entry in parser.get('Removed Associations',kind).split(';') if entry and entry not in ('omarchy-prism.desktop','ky-prism.desktop')]
                parser.set('Removed Associations',kind,';'.join(entries)+';' if entries else '')
        staged=backup/'mimeapps-new.list'
        with staged.open('w') as stream:parser.write(stream,space_around_delimiters=False)
        files.append((staged,mime))
        shell=home/'.config/omarchy/shell.json'
        if shell.exists():
            if any(p.is_symlink() for p in [shell,*shell.parents]):raise ValueError('Symlink konfiguracji paska')
            staged=backup/'shell-new.json';staged.write_text(json.dumps(shell_layout(json.loads(shell.read_text())),indent=2,ensure_ascii=False)+'\n');files.append((staged,shell))
        previous={}; written=[]
        try:
            for src,dst in files:
                rel=dst.relative_to(home);previous[str(rel)]=dst.exists()
                if dst.exists():
                    old=backup/rel;old.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(dst,old)
                dst.parent.mkdir(parents=True,exist_ok=True)
                fd,tmp=tempfile.mkstemp(prefix='.prism-',dir=dst.parent);os.close(fd)
                try:
                    shutil.copy2(src,tmp)
                    if src.name in ('ky-prism','omarchy-prism','omarchy-prism-open','omarchy-prism-terminal','omarchy-prism-chrome','run.sh'): os.chmod(tmp,0o755)
                    if dst in (favorite_file,bookmark_file,language_file):os.chmod(tmp,0o600)
                    os.replace(tmp,dst);written.append(dst)
                finally:
                    Path(tmp).unlink(missing_ok=True)
            if live_hypr and config.exists():
                subprocess.run(['hyprctl','reload'],check=True,capture_output=True,timeout=10)
                errors=subprocess.run(['hyprctl','-j','configerrors'],check=True,capture_output=True,text=True,timeout=10)
                diagnostics=configuration_errors(errors.stdout)
                if diagnostics: raise ValueError('Błędy konfiguracji Hyprland: '+ '\n'.join(diagnostics)[:2048])
            record={str(dst.relative_to(home)):digest(dst) for _,dst in files if dst not in (config,mime,shell,favorite_file,bookmark_file,language_file)}
            (backup/'previous.json').write_text(json.dumps(previous,indent=2)+'\n')
            target=home/'.local/share/omarchy-prism/installed.json'
            fd,tmp=tempfile.mkstemp(prefix='.installed-',dir=target.parent)
            try:
                with os.fdopen(fd,'w') as stream:
                    stream.write(json.dumps(record,indent=2)+'\n');stream.flush();os.fsync(stream.fileno())
                os.replace(tmp,target)
            finally: Path(tmp).unlink(missing_ok=True)
        except BaseException:
            for dst in reversed(written):
                rel=dst.relative_to(home)
                if previous[str(rel)]: shutil.copy2(backup/rel,dst)
                else: dst.unlink(missing_ok=True)
            if live_hypr and config.exists(): subprocess.run(['hyprctl','reload'],capture_output=True,timeout=10,check=False)
            raise
        if shutil.which('update-desktop-database'): subprocess.run(['update-desktop-database',str(home/'.local/share/applications')],check=False)
        if live_hypr and shutil.which('omarchy-shell'):
            try:
                refreshed=subprocess.run(['omarchy-shell','shell','rescanPlugins'],capture_output=True,text=True,timeout=15)
                if refreshed.returncode:
                    print('Pliki pluginów zapisane; nie potwierdzono odświeżenia paska. W sesji pulpitu wykonaj: omarchy restart shell',file=sys.stderr)
            except (OSError,subprocess.TimeoutExpired):
                print('Nie potwierdzono odświeżenia paska. W sesji pulpitu wykonaj: omarchy restart shell',file=sys.stderr)
        print('Zainstalowano ky-prism. Kopia poprzednich plików:',backup)
        print('Uruchom: ky-prism /ścieżka/zdjęcie.jpg — sprawdź nazwę renderera w stopce.')
    elif action=='verify':
        record=json.loads((home/'.local/share/omarchy-prism/installed.json').read_text())
        for name,expected in record.items():
            if digest(home/name)!=expected: raise ValueError('Niezgodność: '+name)
        print('Pliki instalacji zgodne z rejestrem.')
    elif action in ('export','backup'):
        if len(sys.argv)!=3: raise ValueError('Podaj katalog docelowy.')
        dest=Path(sys.argv[2]).resolve();dest.mkdir(parents=True,exist_ok=True)
        stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
        archive=dest/f'ky-prism-{action}-{stamp}.tar.gz'
        with tempfile.TemporaryDirectory(prefix='prism-package-') as tmp:
            stage=Path(tmp)/'ky-prism';stage.mkdir()
            for name in SOURCES:
                src=ROOT/name
                if src.is_dir():shutil.copytree(src,stage/name,ignore=shutil.ignore_patterns('__pycache__'))
                else:
                    (stage/name).parent.mkdir(parents=True,exist_ok=True)
                    shutil.copy2(src,stage/name)
            favorites=home/'.local/share/omarchy-prism/favorites.json'
            if favorites.is_file():
                if favorites.is_symlink() or favorites.stat().st_size>8*1024*1024:raise ValueError('Nieprawidłowa kopia ulubionych')
                (stage/'user-data').mkdir(exist_ok=True);shutil.copy2(favorites,stage/'user-data/favorites.json')
            bookmarks=home/'.local/share/omarchy-prism/bookmarks.json'
            if bookmarks.is_file():
                if bookmarks.is_symlink() or bookmarks.stat().st_size>128*1024:raise ValueError('Nieprawidłowa kopia zakładek')
                (stage/'user-data').mkdir(exist_ok=True);shutil.copy2(bookmarks,stage/'user-data/bookmarks.json')
            language=home/'.local/share/omarchy-prism/language.json'
            if language.is_file():
                if language.is_symlink() or language.stat().st_size>4096:raise ValueError('Nieprawidłowa kopia języka')
                preference=json.loads(language.read_text())
                if preference.get('version')!=1 or preference.get('language') not in ('pl','en'):raise ValueError('Nieprawidłowy język')
                (stage/'user-data').mkdir(exist_ok=True)
                (stage/'user-data/language.json').write_text(json.dumps({'version':1,'language':preference['language']})+'\n')
            appearance=home/'.config/ky-prism/appearance.json'
            if appearance.exists() or appearance.is_symlink():
                if appearance.is_symlink() or appearance.stat().st_size>4096: raise ValueError('Nieprawidłowe ustawienia wyglądu')
                preference=json.loads(appearance.read_text())
                percent=preference.get('ui_opacity_percent') if isinstance(preference,dict) else None
                if type(percent) is not int or not 0<=percent<=100: raise ValueError('Nieprawidłowe krycie interfejsu')
                (stage/'user-data').mkdir(exist_ok=True)
                (stage/'user-data/appearance.json').write_text(json.dumps({'ui_opacity_percent':percent})+'\n')
            if (ROOT/'build/ky-prism').is_file():
                (stage/'prebuilt').mkdir();shutil.copy2(ROOT/'build/ky-prism',stage/'prebuilt/ky-prism')
                module=nvjpeg_artifact()
                if module: shutil.copy2(module,stage/'prebuilt/libprism_nvjpeg.so')
                metadata=nvjpeg_metadata()
                if metadata: shutil.copy2(metadata,stage/'prebuilt/nvjpeg-build.json')
            if action=='backup' and (home/'.local/share/omarchy-prism/installed.json').exists():
                record=json.loads((home/'.local/share/omarchy-prism/installed.json').read_text())
                for name in record:
                    out=stage/'installed'/name;out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(home/name,out)
            manifest={str(p.relative_to(stage)):digest(p) for p in stage.rglob('*') if p.is_file()}
            (stage/'MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
            with tarfile.open(archive,'x:gz') as tar:tar.add(stage,arcname='ky-prism')
        os.chmod(archive,0o600)
        archive.with_suffix(archive.suffix+'.sha256').write_text(digest(archive)+'  '+archive.name+'\n')
        print(archive);print('Źródła + binarka dla bieżącego systemu; paczki offline zapewnia pełny eksport Omarchy. Dla Ubuntu: ./run.sh photos '+action+' KATALOG --target ubuntu (z katalogu remote-desktop).')
    else:raise ValueError('Nieznana operacja')
if __name__=='__main__':
    try: main()
    except Exception as exc: print(str(exc),file=sys.stderr);sys.exit(1)
