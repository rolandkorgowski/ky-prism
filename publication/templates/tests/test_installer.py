import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('public_installer',ROOT/'tools/install.py')
app=importlib.util.module_from_spec(spec);spec.loader.exec_module(app)

class Installer(unittest.TestCase):
    def test_root_install_is_refused_before_dependency_or_repository_changes(self):
        with patch.object(sys,'argv',['install.py','--setup-rhel-repos']),patch.object(app.os,'geteuid',return_value=0),patch.object(app,'system_info',return_value=dict(distro='rhel',version='9',family='dnf',major='9')),patch.object(app,'verify_source'),patch.object(app,'setup_rhel') as setup,patch.object(app,'dependencies') as deps:
            with self.assertRaisesRegex(ValueError,'without sudo'):app.main()
            setup.assert_not_called();deps.assert_not_called()

    def test_four_requested_distributions_and_package_plans(self):
        for distro,version,manager in [('ubuntu','24.04','apt-get'),('ubuntu','26.04','apt-get'),('ubuntu','26.04.1','apt-get'),('rhel','9.6','dnf'),('rhel','10.1','dnf')]:
            with self.subTest(distro=distro,version=version):
                info=app.system_info('ID="'+distro+'"\nVERSION_ID="'+version+'"\n')
                command=app.package_plan(info)
                self.assertEqual(command[0],manager)
                self.assertIn('libvips-dev' if manager=='apt-get' else 'vips-devel',command)
                self.assertIn('udisks2',command)
                self.assertFalse(any('cuda' in x or 'nvidia' in x or 'upgrade' in x for x in command))
        for text in ['ID=ubuntu\nVERSION_ID=22.04','ID=rhel\nVERSION_ID=8.10','ID=unknown']:
            with self.assertRaises(ValueError):app.system_info(text)

    def test_install_update_cpu_removes_old_cuda_adapter_uninstall(self):
        with tempfile.TemporaryDirectory(prefix='ky-public-') as tmp:
            base=Path(tmp);prefix=base/'konto żółć';stage=base/'stage';(stage/'bin').mkdir(parents=True)
            (stage/'bin/ky-prism').write_text('first');(stage/'bin/ky-prism').chmod(0o755)
            lib=stage/'libexec/ky-prism';lib.mkdir(parents=True);(lib/'libprism_nvjpeg.so').write_bytes(b'optional adapter')
            app.apply_stage(stage,prefix)
            self.assertEqual((prefix/'bin/ky-prism').stat().st_mode&0o777,0o755)
            (lib/'libprism_nvjpeg.so').unlink();(stage/'bin/ky-prism').write_text('second')
            app.apply_stage(stage,prefix)
            self.assertFalse((prefix/'libexec/ky-prism/libprism_nvjpeg.so').exists())
            self.assertEqual((prefix/'bin/ky-prism').read_text(),'second')
            unrelated=prefix/'notes.txt';unrelated.write_text('keep')
            app.uninstall(prefix)
            self.assertFalse((prefix/'bin/ky-prism').exists());self.assertEqual(unrelated.read_text(),'keep')

    def test_modified_unmanaged_and_symlink_files_are_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);stage=base/'stage';(stage/'bin').mkdir(parents=True);(stage/'bin/ky-prism').write_text('our app')
            prefix=base/'user';(prefix/'bin').mkdir(parents=True);p=prefix/'bin/ky-prism';p.write_text('another app')
            with self.assertRaises(ValueError):app.apply_stage(stage,prefix)
            self.assertEqual(p.read_text(),'another app');p.unlink()
            app.apply_stage(stage,prefix);p.write_text('personal change')
            with self.assertRaises(ValueError):app.uninstall(prefix)
            with self.assertRaises(ValueError):app.apply_stage(stage,prefix)
            target=base/'target';target.write_text('do not touch');p.unlink();p.symlink_to(target)
            with self.assertRaises(ValueError):app.apply_stage(stage,prefix)
            self.assertEqual(target.read_text(),'do not touch')

    def test_failed_update_restores_previous_files_and_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);stage=base/'stage';(stage/'bin').mkdir(parents=True);(stage/'bin/ky-prism').write_text('first')
            prefix=base/'prefix';app.apply_stage(stage,prefix);before=(prefix/'share/ky-prism/install.json').read_bytes()
            (stage/'bin/ky-prism').write_text('updated')
            original=app.atomic;failed=[False]
            def fail_receipt(path,data,mode=0o644):
                if path.name=='install.json' and not failed[0]:
                    failed[0]=True;raise OSError('disk full')
                return original(path,data,mode)
            with patch.object(app,'atomic',side_effect=fail_receipt):
                with self.assertRaises(OSError):app.apply_stage(stage,prefix)
            self.assertEqual((prefix/'bin/ky-prism').read_text(),'first')
            self.assertEqual((prefix/'share/ky-prism/install.json').read_bytes(),before)

    def test_receipt_cannot_escape_prefix(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix=Path(tmp);p=prefix/'share/ky-prism/install.json';p.parent.mkdir(parents=True);p.write_text(json.dumps({'../../victim':'a'*64}))
            with self.assertRaises(ValueError):app.uninstall(prefix)

    def test_signing_key_parser_requires_one_primary_key(self):
        class Result:stdout='pub:one\nfpr:::::::::ABC:\nsub:one\nfpr:::::::::DEF:\n'
        with patch.object(app.subprocess,'run',return_value=Result()):self.assertEqual(app.key_fingerprint(Path('key'),Path('home')),'ABC')
        Result.stdout+='pub:two\nfpr:::::::::BAD:\n'
        with patch.object(app.subprocess,'run',return_value=Result()):
            with self.assertRaises(ValueError):app.key_fingerprint(Path('key'),Path('home'))

    def test_no_signature_bypass_and_explicit_nvjpeg_auto(self):
        source=(ROOT/'tools/install.py').read_text();cmake=(ROOT/'CMakeLists.txt').read_text();launcher=(ROOT/'ky-prism').read_text()
        self.assertIn('localpkg_gpgcheck=1',source)
        self.assertNotIn('--nogpgcheck',source)
        self.assertIn('set(PRISM_WITH_NVJPEG AUTO',cmake)
        self.assertIn('PRISM_JPEG_BACKEND=${PRISM_JPEG_BACKEND:-auto}',launcher)
        self.assertNotIn('LD_LIBRARY_PATH',launcher)

    def test_cuda_install_is_opt_in_ubuntu_and_does_not_add_driver_repo(self):
        with patch.object(app,'privileged') as commands:
            app.install_cuda({'family':'apt','version':'26.04.1'})
            self.assertEqual(commands.call_args_list[-1].args[0],['apt-get','install','--no-install-recommends','--no-remove','cuda-toolkit'])
            self.assertEqual(app.cuda_package({'version':'24.04'}),'nvidia-cuda-toolkit')
            with self.assertRaises(ValueError):app.install_cuda({'family':'arch'})
            self.assertEqual(commands.call_count,2)

    def test_runtime_plugins_are_checked_even_when_development_libraries_exist(self):
        class Result: returncode=1;stdout=''
        with patch.object(app.subprocess,'run',return_value=Result()):
            self.assertIn('libgtk-4-1',app.missing_desktop_packages({'family':'apt'}))
            self.assertEqual(app.missing_desktop_packages({'family':'arch'}),[])

    def test_build_jobs_respect_ram_and_cores(self):
        with patch.object(app.os,'sched_getaffinity',return_value=set(range(16))),patch.object(app.os,'sysconf',side_effect=[8*1024**3//4096,4096]):
            self.assertEqual(app.build_jobs(),2)
        with patch.object(app.os,'sched_getaffinity',return_value=set(range(16))),patch.object(app.os,'sysconf',side_effect=[32*1024**3//4096,4096]):
            self.assertEqual(app.build_jobs(),8)

if __name__=='__main__':unittest.main()
