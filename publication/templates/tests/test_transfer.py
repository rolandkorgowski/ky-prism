import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('transfer',ROOT/'tools/transfer.py')
app=importlib.util.module_from_spec(spec);spec.loader.exec_module(app)

class Transfer(unittest.TestCase):
    def test_opacity_configuration_round_trip_and_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);root=base/'source';root.mkdir();(root/'main.cpp').write_text('int main(){}');app.seal(root)
            config=base/'config';(config/'ky-prism').mkdir(parents=True)
            appearance=config/'ky-prism/appearance.json'
            appearance.write_text('{"ui_opacity_percent":95,"unrelated":"omit"}')
            with patch.dict(os.environ,{'XDG_CONFIG_HOME':str(config),'XDG_DATA_HOME':str(base/'data')}):
                archive=app.export(root,base/'out')
            unpack=base/'unpack'
            with tarfile.open(archive) as tar:tar.extractall(unpack,filter='data')
            destination=base/'new-config'
            with patch.dict(os.environ,{'XDG_CONFIG_HOME':str(destination),'XDG_DATA_HOME':str(base/'new-data')}):
                app.restore(unpack/'ky-prism')
                restored=destination/'ky-prism/appearance.json'
                self.assertEqual(json.loads(restored.read_text()),{'ui_opacity_percent':95})
                restored.write_text('{"ui_opacity_percent":100}')
                app.restore(unpack/'ky-prism')
                self.assertEqual(json.loads(restored.read_text())['ui_opacity_percent'],100)
            for invalid in ('false','{"ui_opacity_percent":true}','{"ui_opacity_percent":101}','{"ui_opacity_percent":94.5}'):
                appearance.write_text(invalid)
                with self.assertRaises(ValueError):app.preference(appearance,4096)

    def test_roundtrip_does_not_copy_photos_build_or_secrets(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);root=base/'source';root.mkdir()
            (root/'main.cpp').write_text('int main() {}')
            app.seal(root)
            (root/'secret.key').write_text('PRIVATE')
            (root/'build').mkdir();(root/'build/app').write_text('Arch ELF')
            data=base/'old data'/'omarchy-prism';data.mkdir(parents=True)
            (data/'favorites.json').write_text('{"version":1,"photos":[]}')
            (data/'language.json').write_text('{"version":1,"language":"en","private":"omit"}')
            (data/'photo.jpg').write_bytes(b'not transferred')
            with patch.dict(os.environ,{'XDG_DATA_HOME':str(data.parent),'XDG_CONFIG_HOME':str(base/'config')}):
                for action in ('export','backup'):
                    archive=app.export(root,base/'out',action)
                    self.assertEqual(app.audit_archive(archive),3)
                    self.assertEqual(app.digest(archive),archive.with_suffix('.gz.sha256').read_text().split()[0])
                extracted=base/'restored'
                with tarfile.open(archive) as tar:
                    self.assertFalse(any('build/' in n or 'secret' in n or 'photo.jpg' in n for n in tar.getnames()))
                    tar.extractall(extracted,filter='data')
            new_data=base/'new data'
            with patch.dict(os.environ,{'XDG_DATA_HOME':str(new_data),'XDG_CONFIG_HOME':str(base/'new-config')}):
                app.restore(extracted/'ky-prism')
                pref=new_data/'omarchy-prism/language.json'
                self.assertEqual(json.loads(pref.read_text()),{'version':1,'language':'en'})
                pref.write_text('{"version":1,"language":"pl"}')
                app.restore(extracted/'ky-prism')
                self.assertEqual(json.loads(pref.read_text())['language'],'pl')

    def test_tamper_and_symlink_are_refused_without_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'source';root.mkdir();p=root/'a';p.write_text('original');app.seal(root)
            p.write_text('changed')
            with self.assertRaises(ValueError): app.export(root,Path(tmp)/'out')
            p.unlink();p.symlink_to('/etc/passwd')
            with self.assertRaises(ValueError): app.verify(root)
            (root/'SOURCE-MANIFEST.json').write_text(json.dumps({'../outside':'x'*64}))
            with self.assertRaises(ValueError): app.verify(root)
            self.assertFalse((Path(tmp)/'out').exists())

    def test_preferences_reject_links_oversized_and_invalid_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            data=Path(tmp)/'data';data.mkdir();p=data/'language.json'
            for content in ('false','{"version":1,"language":"fr"}','x'*5000):
                p.write_text(content)
                with self.assertRaises(ValueError): app.preference(p,4096)
            p.unlink();p.symlink_to('/etc/passwd')
            with self.assertRaises(ValueError): app.preference(p,4096)

    def test_native_ada_and_portable_cpu_build(self):
        cmake=(ROOT/'CMakeLists.txt').read_text()
        self.assertIn('75-real;89-real;75-virtual',cmake)
        self.assertNotIn('-march=native',cmake)
        self.assertNotIn('-mavx512',cmake)

if __name__=='__main__': unittest.main()
