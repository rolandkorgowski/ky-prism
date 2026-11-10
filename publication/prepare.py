#!/usr/bin/env python3
"""Prepare an allowlisted standalone GitHub tree, never a private-account export."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tarfile
import tempfile

HERE=Path(__file__).resolve().parent
SOURCE=HERE.parent
EXECUTABLES={'install.sh','run.sh','ky-prism','tools/install.py','tools/transfer.py'}
TEMPLATES=('CMakeLists.txt','install.sh','ky-prism','LICENSE','COPYRIGHT.md','README.md',
           'THIRD_PARTY_NOTICES.md','SECURITY.md','.gitignore','.gitattributes',
           'tools/install.py','.github/workflows/build.yml','.github/workflows/rhel-self-hosted.yml',
           'docs/RHEL.md','docs/LICENSING.md','docs/TESTING.md','docs/PUBLISHING.md',
           'tests/test_installer.py','tests/auto_fallback.cpp',
           'LICENSES/LGPL-3.0.txt','LICENSES/GPL-3.0.txt','LICENSES/LGPL-2.1.txt',
           'LICENSES/libjpeg-turbo-LICENSE.md','LICENSES/JPEG-IJG.txt',
           'LICENSES/Little-CMS-MIT.txt','LICENSES/zlib.txt')
TEMPLATES += ('run.sh','tools/transfer.py','docs/TRANSFER-PL.md','tests/test_transfer.py')
TESTS=('decode.cpp','exif_fixture.h','file_dialog.cpp','photo_dialog.cpp','favorites.cpp','file_ops.cpp',
       'raster_source.cpp','thumbnails.cpp','material.cpp','pointer.cpp','gallery.cpp',
       'pixel_simd.cpp','interaction.cpp','ui.cpp','keyboard.cpp','browser.cpp','language.cpp',
       'shader.cpp','thumbnail_shader.cpp','nvjpeg.cpp','test_language_catalog.py','shutdown.cpp')
TESTS += ('appearance.cpp',)
TESTS += ('gtk_ui.cpp','gtk_settings.cpp')
HEADER='// SPDX-License-Identifier: MIT\n// Copyright (c) 2026 Krzysztof Kylen Lenartowicz\n'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def public_icon():
    # Same original seven-blade ProductMark as About, with no account data.
    return (SOURCE/'omarchy-prism.svg').read_text()

def stage(output, *, personal=False, archive=True):
    if output.exists() or output.is_symlink(): raise ValueError('Output already exists; use a new empty destination: '+str(output))
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.ky-prism-public-',dir=output.parent) as tmp:
        folder=Path(tmp)/'ky-prism';folder.mkdir()
        for name in TEMPLATES:
            source=HERE/'templates'/name
            if any(p.is_symlink() for p in (source,*source.parents)): raise ValueError('Symlink in public template path: '+name)
            target=folder/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
        for sub in ('src','src/gtk','tests'): (folder/sub).mkdir(exist_ok=True)
        (folder/'i18n').mkdir()
        for name in ('en.json','generate.py'):
            shutil.copy2(SOURCE/'i18n'/name,folder/'i18n'/name)
        for path in sorted((SOURCE/'src').iterdir()):
            if path.is_symlink(): raise ValueError('Unexpected source symlink: '+path.name)
            if path.suffix in ('.cpp','.h','.cu') or path.name=='NVIDIA-NVJPEG-NOTICE.txt':
                shutil.copy2(path,folder/'src'/path.name)
        for path in sorted((SOURCE/'src/gtk').iterdir()):
            if path.is_symlink(): raise ValueError('Unexpected source symlink: '+path.name)
            if path.suffix in ('.cpp','.h'): shutil.copy2(path,folder/'src/gtk'/path.name)
        for name in TESTS: shutil.copy2(SOURCE/'tests'/name,folder/'tests'/name)
        shutil.copy2(SOURCE/'diagnose-hang.py',folder/'tools/diagnose-hang.py')
        for name in ('ky-prism.desktop','KEYBOARD.md'): shutil.copy2(SOURCE/name,folder/name)
        # Public controls use a fresh generic geometric photo glyph. No image
        # reference, Chrome brand mark, bar animation or private skin is copied.
        icon=folder/'src/ui_icons.h';original_icon=icon.read_text();text=original_icon
        a=text.index('    // Layered camera inspired by the supplied illustrated icon sheet.')
        b=text.index('  } else if (name == "bookmarks"',a)
        text=text[:a]+'''    p.setBrush(paper);
    p.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
    p.setBrush(accent);
    p.drawEllipse(QPointF(16, 9), 1.7, 1.7);
    p.setBrush(Qt::NoBrush);
    QPainterPath photo;
    photo.moveTo(4, 17); photo.lineTo(9, 11); photo.lineTo(13, 15);
    photo.lineTo(16, 12); photo.lineTo(20, 17);
    p.drawPath(photo);
'''+text[b:];icon.write_text(text)
        about=folder/'src/ui_extensions.cpp';text=about.read_text();old='"CMS\\nBiblioteki zewnętrzne zachowują własne prawa i licencje."'
        if old not in text: raise ValueError('About notice changed; review the public license notice merge')
        text=text.replace(old,'"CMS\\nWłasny kod: MIT. Qt: LGPLv3; libvips: LGPL2.1+; LibRaw: LGPL2.1.\\nOpcjonalne CUDA/nvJPEG: licencja NVIDIA."')
        about.write_text(text)
        # The GTK front end carries the same two public-variant changes.
        gtk_icon=folder/'src/gtk/icons.cpp';original_gtk_icon=gtk_icon.read_text();text=original_gtk_icon
        a=text.index('  if (name == "openPhoto") {\n')+len('  if (name == "openPhoto") {\n')
        b=text.index('  } else if (name == "bookmarks" || name == "favorites") {',a)
        text=text[:a]+'''    p.setBrush(paper);
    p.roundedRect(3, 4, 18, 16, 2);
    p.setBrush(accent);
    p.circle(16, 9, 1.7);
    p.noBrush();
    p.polyline({4, 17, 9, 11, 13, 15, 16, 12, 20, 17});
'''+text[b:];gtk_icon.write_text(text)
        gtk_about=folder/'src/gtk/dialogs.cpp';original_gtk_about=gtk_about.read_text();text=original_gtk_about
        if text.count(old)!=1: raise ValueError('GTK About notice changed; review the public license notice merge')
        text=text.replace(old,'"CMS\\nWłasny kod: MIT. GTK: LGPL2.1+; Qt: LGPLv3; libvips: LGPL2.1+; LibRaw: LGPL2.1.\\nOpcjonalne CUDA/nvJPEG: licencja NVIDIA."')
        gtk_about.write_text(text)
        if personal:
            # A private migration must preserve the user's UI and copyright.
            gtk_icon.write_text(original_gtk_icon)
            gtk_about.write_text(original_gtk_about)
            icon.write_text(original_icon)
            shutil.copy2(SOURCE/'src/ui_extensions.cpp',about)
            shutil.copy2(SOURCE/'COPYRIGHT.md',folder/'COPYRIGHT.md')
            (folder/'LICENSE').write_text((SOURCE/'COPYRIGHT.md').read_text()+
                '\nPersonal migration copy. This transfer does not grant a new public license.\n')
            readme=folder/'README.md'
            text=readme.read_text().replace('Free to use and share.','Personal migration package.')
            text=text.replace('Own code is under the\n[MIT license](LICENSE).','This personal copy retains the original [copyright](COPYRIGHT.md).')
            readme.write_text(text)
            publishing=folder/'docs/PUBLISHING.md'
            publishing.write_text('# Private transfer\n\nThis package may include personal favourites and paths. '
                                  'Do not publish it. Prepare a separate public source release and review its license.\n')
            notices=folder/'THIRD_PARTY_NOTICES.md'
            notices.write_text(notices.read_text().replace('the ky-prism MIT license','any license to original ky-prism code'))
        (folder/'ky-prism.svg').write_text(public_icon())
        for path in [*(folder/'src').iterdir(),*(folder/'src/gtk').iterdir()]:
            if not personal and path.suffix in ('.cpp','.h','.cu') and not path.read_text().startswith(HEADER): path.write_text(HEADER+path.read_text())
        for path in (folder/'tests').iterdir():
            if not personal and path.suffix in ('.cpp','.h'): path.write_text(HEADER+path.read_text())
        for path in sorted(folder.rglob('*')):
            if path.is_symlink(): raise ValueError('Unexpected publication symlink')
            if path.is_file():
                data=path.read_bytes()
                for private in (b'/home/kylen/',b'Kopie-Omarchy',b'20-nvidia-render.conf'):
                    if private in data: raise ValueError('Private-machine reference in publication: '+str(path.relative_to(folder)))
                os.chmod(path,0o755 if str(path.relative_to(folder)) in EXECUTABLES else 0o644)
        manifest={str(p.relative_to(folder)):digest(p) for p in sorted(folder.rglob('*')) if p.is_file()}
        (folder/'SOURCE-MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
        os.replace(folder,output)
    if not archive: return output
    archive=output.with_suffix('.tar.gz')
    with tarfile.open(archive,'x:gz') as tar:
        tar.add(output,arcname='ky-prism')
    archive.with_suffix('.gz.sha256').write_text(digest(archive)+'  '+archive.name+'\n')
    return archive

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=SOURCE/'exports/github/ky-prism')
    args=parser.parse_args()
    archive=stage(args.output.absolute())
    print('Standalone GitHub folder:',args.output.absolute())
    print('Source archive:',archive)
    print('Proposed license: MIT; review before publication. No GitHub publication performed.')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError) as error: raise SystemExit(str(error))
