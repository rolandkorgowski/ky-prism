import importlib.util
import hashlib
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
import shutil
from unittest.mock import patch

HERE=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('public_preparer',HERE/'prepare.py')
app=importlib.util.module_from_spec(spec);spec.loader.exec_module(app)

class Publication(unittest.TestCase):
    def test_personal_transfer_preserves_ui_and_copyright(self):
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'personal';app.stage(output,personal=True,archive=False)
            for name in ('src/ui_icons.h','src/ui_extensions.cpp','src/gtk/icons.cpp','src/gtk/dialogs.cpp','COPYRIGHT.md'):
                self.assertEqual((output/name).read_bytes(),(app.SOURCE/name).read_bytes())
            self.assertNotIn('Own code is under the',(output/'README.md').read_text())
            self.assertNotIn('MIT License',(output/'LICENSE').read_text())
            self.assertTrue((output/'run.sh').is_file())
            self.assertFalse(output.with_suffix('.tar.gz').exists())

    def test_allowlisted_source_archive_has_no_account_data_or_binaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'ky-prism';archive=app.stage(output)
            manifest=json.loads((output/'SOURCE-MANIFEST.json').read_text())
            self.assertTrue((output/'LICENSE').read_text().startswith('MIT License'))
            self.assertIn('Krzysztof Kylen Lenartowicz',(output/'COPYRIGHT.md').read_text())
            self.assertIn('nvJPEG',(output/'README.md').read_text())
            self.assertIn('default=\'AUTO\'',(output/'tools/install.py').read_text())
            for name,sha in manifest.items():
                self.assertEqual(hashlib.sha256((output/name).read_bytes()).hexdigest(),sha,name)
                self.assertFalse(set(Path(name).parts)&{'.config','.ssh','.git','reports','build','prebuilt','user-data','terminal-plugin','chrome-plugin'})
                self.assertNotIn((output/name).suffix,('.so','.a','.deb','.rpm'))
            self.assertNotIn('inspired by',(output/'src/ui_icons.h').read_text())
            self.assertIn('p.polyline({4, 17, 9, 11, 13, 15, 16, 12, 20, 17});',(output/'src/gtk/icons.cpp').read_text())
            self.assertIn('GTK: LGPL2.1+',(output/'src/gtk/dialogs.cpp').read_text())
            self.assertIn('src/gtk/window.cpp',manifest)
            self.assertIn('tests/gtk_ui.cpp',manifest)
            with tarfile.open(archive) as tar:
                self.assertIn('ky-prism/.github/workflows/build.yml',tar.getnames())
                self.assertIn('ky-prism/tests/test_installer.py',tar.getnames())
            with self.assertRaises(ValueError):app.stage(output)

    def test_workflow_has_all_requested_build_environments_and_pins_actions(self):
        workflow=(HERE/'templates/.github/workflows/build.yml').read_text()
        for image in ('ubuntu:24.04','ubuntu:26.04','almalinux:9','almalinux:10'):self.assertIn(image,workflow)
        self.assertIn('PRISM_WITH_NVJPEG=OFF',workflow)
        self.assertIn('git archive',workflow)
        self.assertIn('permissions:\n  contents: read',workflow)
        import re
        for action in re.findall(r'uses: (\S+)',workflow):self.assertRegex(action,r'@[0-9a-f]{40}$')

    def test_unlisted_template_files_are_not_published(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);templates=base/'templates';shutil.copytree(HERE/'templates',templates)
            (templates/'.env').write_text('PRIVATE_TOKEN=do-not-publish')
            (templates/'personal-notes.md').write_text('do-not-publish')
            (templates/'secret.key').symlink_to(templates/'.env')
            with patch.object(app,'HERE',base):app.stage(base/'output')
            for name in ('.env','personal-notes.md','secret.key'):self.assertFalse((base/'output'/name).exists())

if __name__=='__main__':unittest.main()
