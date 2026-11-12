"""backup / restore / export / import of ky-prism (prismctl.py)."""
import io
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Transfer(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.home = base / 'home'
        self.backups = base / 'backups'
        self.env = dict(os.environ, HOME=str(self.home), PRISM_BACKUP_DIR=str(self.backups))
        for key in ('XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'HYPRLAND_INSTANCE_SIGNATURE'):
            self.env.pop(key, None)
        self.data = self.home / '.local/share/omarchy-prism'
        self.config = self.home / '.config/ky-prism'
        self.data.mkdir(parents=True)
        self.config.mkdir(parents=True)
        (self.data / 'favorites.json').write_text('{"version":1,"photos":[]}')
        (self.data / 'bookmarks.json').write_text('["/zdjęcia"]')
        (self.data / 'language.json').write_text('{"version":1,"language":"en","extra":"dropped"}')
        (self.config / 'appearance.json').write_text('{"ui_opacity_percent":80}')

    def tearDown(self):
        self.tmp.cleanup()

    def ctl(self, *args, env=None, good=True):
        result = subprocess.run(['python3', '-B', str(ROOT / 'prismctl.py'), *args], env=env or self.env,
                                capture_output=True, text=True, stdin=subprocess.DEVNULL)
        if good:
            self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def test_settings_backup_and_restore(self):
        self.ctl('backup', '--ustawienia')
        archive = next(self.backups.glob('ky-prism-ustawienia-*.tar.gz'))
        self.assertEqual(archive.stat().st_mode & 0o777, 0o600)
        with tarfile.open(archive) as tar:
            names = set(tar.getnames())
            self.assertEqual(json.load(tar.extractfile('ky-prism-ustawienia/data/language.json')),
                             {'version': 1, 'language': 'en'})
        self.assertIn('ky-prism-ustawienia/config/appearance.json', names)
        self.assertIn('ky-prism-ustawienia/USTAWIENIA.txt', names)
        # Restore into a changed account: current settings are saved first.
        (self.data / 'language.json').write_text('{"version":1,"language":"pl"}')
        (self.data / 'bookmarks.json').unlink()
        self.ctl('restore', str(archive))
        self.assertEqual(json.loads((self.data / 'language.json').read_text())['language'], 'en')
        self.assertEqual((self.data / 'bookmarks.json').read_text(), '["/zdjęcia"]')
        self.assertEqual((self.data / 'bookmarks.json').stat().st_mode & 0o777, 0o600)
        safety = list(self.backups.glob('ky-prism-ustawienia-przed-przywroceniem-*.tar.gz'))
        self.assertEqual(len(safety), 1)

    def test_backups_never_overwrite(self):
        self.ctl('backup', '--ustawienia')
        self.ctl('backup', '--ustawienia')
        self.assertEqual(len(list(self.backups.glob('ky-prism-ustawienia-*.tar.gz'))), 2)

    def test_export_without_settings_and_import_message(self):
        out = Path(self.tmp.name) / 'dist'
        self.ctl('export', '--system', '--bez-ustawien', '--katalog', str(out))
        archive = next(out.glob('*.tar.gz'))
        self.assertEqual(archive.stat().st_mode & 0o777, 0o644)
        with tarfile.open(archive) as tar:
            self.assertFalse(any('/ustawienia/' in n for n in tar.getnames()))
            text = tar.extractfile(tar.getnames()[0].split('/')[0] + '/INSTALACJA.txt').read().decode()
        for system in ('Ubuntu 24.04 / 26.04', 'Arch Linux, Omarchy', 'FreeBSD', 'OpenBSD'):
            self.assertIn(system, text)

    def test_program_backup_contains_settings_and_restores_from_it(self):
        self.ctl('backup', '--program')
        archive = next(self.backups.glob('ky-prism-*-kopia-programu-*.tar.gz'))
        with tarfile.open(archive) as tar:
            names = tar.getnames()
        top = names[0].split('/')[0]
        self.assertIn(top + '/ustawienia/ky-prism-ustawienia.tar.gz', names)
        self.assertIn(top + '/MANIFEST.json', names)
        if (ROOT / 'build/ky-prism').is_file():
            self.assertIn(top + '/prebuilt/ky-prism', names)
            self.assertIn(top + '/prebuilt/SYSTEM.json', names)
        (self.data / 'language.json').write_text('{"version":1,"language":"pl"}')
        self.ctl('restore', str(archive))
        self.assertEqual(json.loads((self.data / 'language.json').read_text())['language'], 'en')

    def test_hostile_archives_are_refused(self):
        evil = Path(self.tmp.name) / 'evil.tar.gz'
        with tarfile.open(evil, 'w:gz') as tar:
            data = b'{"version":1,"language":"pl"}'
            info = tarfile.TarInfo('../../escape.json')
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
        result = self.ctl('restore', str(evil), good=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((Path(self.tmp.name) / 'escape.json').exists())
        wrong = Path(self.tmp.name) / 'wrong.tar.gz'
        with tarfile.open(wrong, 'w:gz') as tar:
            data = b'{"version":1,"language":"de"}'
            info = tarfile.TarInfo('ky-prism-ustawienia/data/language.json')
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
        self.assertNotEqual(self.ctl('restore', str(wrong), good=False).returncode, 0)
        self.assertEqual(json.loads((self.data / 'language.json').read_text())['language'], 'en')

    def test_symlinked_settings_are_not_written_through(self):
        outside = Path(self.tmp.name) / 'outside.json'
        outside.write_text('{}')
        (self.data / 'bookmarks.json').unlink()
        (self.data / 'bookmarks.json').symlink_to(outside)
        archive = Path(self.tmp.name) / 'b.tar.gz'
        with tarfile.open(archive, 'w:gz') as tar:
            data = b'["/x"]'
            info = tarfile.TarInfo('ky-prism-ustawienia/data/bookmarks.json')
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
        self.assertNotEqual(self.ctl('restore', str(archive), good=False).returncode, 0)
        self.assertEqual(outside.read_text(), '{}')

    def test_first_run_without_terminal_changes_nothing(self):
        out = Path(self.tmp.name) / 'dist'
        self.ctl('export', '--system', '--katalog', str(out))
        unpack = Path(self.tmp.name) / 'unpacked'
        unpack.mkdir()
        with tarfile.open(next(out.glob('*.tar.gz'))) as tar:
            tar.extractall(unpack, filter='data')
        package = next(unpack.iterdir())
        other = dict(self.env, HOME=str(Path(self.tmp.name) / 'other'))
        result = subprocess.run(['sh', str(package / 'run.sh')], env=other, capture_output=True, text=True,
                                stdin=subprocess.DEVNULL)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Bez terminala niczego nie zmieniam', result.stdout)
        self.assertFalse((Path(self.tmp.name) / 'other/.local').exists())
        check = subprocess.run(['python3', '-B', str(package / 'package.py'), 'check-source'], capture_output=True,
                               text=True)
        self.assertEqual(check.returncode, 0, check.stderr)


if __name__ == '__main__':
    unittest.main()
