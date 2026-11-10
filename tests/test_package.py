import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class Packaging(unittest.TestCase):
 def test_install_verify_export_and_backup(self):
  with tempfile.TemporaryDirectory() as tmp:
   home=Path(tmp)/'account with space';home.mkdir();dest=Path(tmp)/'packages'
   env=dict(os.environ,HOME=str(home));env.pop('HYPRLAND_INSTANCE_SIGNATURE',None)
   config=home/'.config/hypr/hyprland.lua';config.parent.mkdir(parents=True);config.write_text('-- existing\n')
   mime=home/'.config/mimeapps.list';mime.write_text('[Default Applications]\nimage/jpeg=existing-viewer.desktop;\ntext/plain=editor.desktop;\n[Added Associations]\nimage/jpeg=other.desktop;omarchy-prism.desktop;\n[Removed Associations]\nimage/jpeg=blocked.desktop;omarchy-prism.desktop;ky-prism.desktop;\n')
   def run(*args,good=True):
    p=subprocess.run(['python3','-B',str(ROOT/'package.py'),*args],env=env,capture_output=True,text=True)
    if good:self.assertEqual(p.returncode,0,p.stderr)
    return p
   shell=home/'.config/omarchy/shell.json';shell.parent.mkdir(parents=True,exist_ok=True)
   shell.write_text(json.dumps({'bar':{'layout':{'left':[{'id':'kylen.menu'},{'id':'kylen.files-dolphin'},{'id':'clock'}],'right':[{'id':'sound'}]}}}))
   run('install');run('verify')
   if (ROOT/'build/nvjpeg-build.json').is_file() and json.loads((ROOT/'build/nvjpeg-build.json').read_text()).get('enabled'):self.assertTrue((home/'.local/share/omarchy-prism/lib/libprism_nvjpeg.so').is_file())
   layout=json.loads(shell.read_text())['bar']['layout']
   self.assertEqual([r['id'] for r in layout['left']],['kylen.menu','kylen.files-dolphin','kylen.prism','kylen.prism-terminal','kylen.prism-chrome','clock'])
   self.assertEqual(layout['right'],[{'id':'sound'}])
   self.assertTrue(os.access(home/'.local/bin/omarchy-prism-open',os.X_OK))
   import configparser
   parsed=configparser.ConfigParser();parsed.read(mime)
   self.assertEqual(parsed['Default Applications']['image/jpeg'],'ky-prism.desktop;')
   self.assertEqual(parsed['Default Applications']['text/plain'],'editor.desktop;')
   self.assertEqual(parsed['Added Associations']['image/jpeg'],'other.desktop;ky-prism.desktop;')
   desktop=(home/'.local/share/applications/ky-prism.desktop').read_text()
   self.assertIn('Exec="'+str(home/'.local/bin/ky-prism')+'" %f',desktop)
   self.assertEqual((home/'.local/share/omarchy-prism/bin/ky-prism').read_bytes(),(home/'.local/share/omarchy-prism/bin/omarchy-prism').read_bytes())
   self.assertEqual(parsed['Removed Associations']['image/jpeg'],'blocked.desktop;')
   launch_env=dict(env,XDG_DATA_HOME=str(home/'.local/share'),XDG_CACHE_HOME=str(home/'.cache'),QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='',PRISM_GPU='auto')
   for command in ['ky-prism','omarchy-prism']:
    version=subprocess.run([str(home/'.local/bin'/command),'--version'],env=launch_env,capture_output=True,text=True,timeout=10)
    self.assertEqual(version.returncode,0,version.stderr)
    self.assertEqual(version.stdout.strip(),'ky-prism 0.13.0')
   self.assertIn('Name=ky-prism',desktop)
   self.assertIn('NoDisplay=true',(home/'.local/share/applications/omarchy-prism.desktop').read_text())
   self.assertIn('-- existing',config.read_text());self.assertEqual(config.read_text().count('require("hypr.prism")'),1)
   run('install');self.assertEqual(sum(r['id']=='kylen.prism' for r in json.loads(shell.read_text())['bar']['layout']['left']),1);self.assertEqual(config.read_text().count('require("hypr.prism")'),1)
   favorites=home/'.local/share/omarchy-prism/favorites.json';favorites.write_text('{"version":1,"photos":[]}')
   bookmarks=home/'.local/share/omarchy-prism/bookmarks.json';bookmarks.write_text('["/photos"]')
   language=home/'.local/share/omarchy-prism/language.json';language.write_text('{"version":1,"language":"en","unrelated":"must not be exported"}')
   run('export',str(dest));run('backup',str(dest))
   for archive in dest.glob('*.tar.gz'):
    self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(),Path(str(archive)+'.sha256').read_text().split()[0])
    with tarfile.open(archive) as tar:
     manifest=json.load(tar.extractfile('ky-prism/MANIFEST.json'))
     self.assertEqual(json.load(tar.extractfile('ky-prism/user-data/language.json')),{'version':1,'language':'en'})
     for name,sha in manifest.items():self.assertEqual(hashlib.sha256(tar.extractfile('ky-prism/'+name).read()).hexdigest(),sha)
     for required in ['src/document.cpp','src/thumbnails.cpp','src/nvjpeg_backend.cpp','src/nvjpeg_cuda.cpp','src/nvjpeg_resize.cu','src/nvjpeg_abi.h','src/pixel_simd.cpp','tests/pixel_simd.cpp','tests/nvjpeg.cpp','src/thumbnail_disk.cpp','src/thumbnail_gpu.cpp','src/thumbnail_shaders.h','tests/gallery.cpp','tests/thumbnail_shader.cpp','src/prefetch.cpp','src/raster_source.cpp','src/ui_material.h','src/glass_widgets.h','src/ui_icons.h','src/pointer_gesture.h','tests/pointer.cpp','tests/material.cpp','chrome-plugin/ChromeMark.qml','terminal-plugin/WarpFlight.qml','terminal-plugin/WarpTerminalWidget.qml','terminal-plugin/WarpGlow.frag.qsb','terminal-plugin/WarpGlow.frag','terminal-plugin/WarpGlow.meta.json']:
      self.assertIn(required,manifest)
   # The installed recovery sources must also work when installation starts
   # from an exported bundle whose manifest includes the prebuilt binary.
   extracted=Path(tmp)/'exported';extracted.mkdir()
   with tarfile.open(next(dest.glob('*.tar.gz'))) as tar:tar.extractall(extracted,filter='data')
   bundle=extracted/'ky-prism';(bundle/'build').mkdir()
   import shutil
   shutil.copy2(ROOT/'build/ky-prism',bundle/'build/ky-prism')
   restored=Path(tmp)/'restored';restored.mkdir();restore_env=dict(env,HOME=str(restored))
   for script,action in [(bundle/'package.py','install'),(restored/'.local/share/omarchy-prism/source/package.py','check-source')]:
    result=subprocess.run(['python3','-B',str(script),action],env=restore_env,capture_output=True,text=True)
    self.assertEqual(result.returncode,0,result.stderr)
   self.assertEqual((restored/'.local/share/omarchy-prism/bookmarks.json').read_bytes(),bookmarks.read_bytes())
   restored_language=restored/'.local/share/omarchy-prism/language.json'
   self.assertEqual(json.loads(restored_language.read_text())['language'],'en')
   restored_language.write_text('{"version":1,"language":"pl"}')
   self.assertTrue(os.access(restored/'.local/bin/omarchy-prism-terminal',os.X_OK))
   restored_favorites=restored/'.local/share/omarchy-prism/favorites.json'
   self.assertEqual(restored_favorites.read_bytes(),favorites.read_bytes())
   restored_favorites.write_text('{"version":1,"photos":[],"keep":true}')
   result=subprocess.run(['python3','-B',str(bundle/'package.py'),'install'],env=restore_env,capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stderr);self.assertIn('keep',restored_favorites.read_text())
   self.assertEqual(json.loads(restored_language.read_text())['language'],'pl')
   (home/'.local/bin/ky-prism').write_text('corrupt')
   self.assertNotEqual(run('verify',good=False).returncode,0)
 def test_hyprland_empty_messages_and_actual_errors(self):
  for response,success in [('[]',True), ('[""]',True), ('[" ","\\n"]',True), ('["", "invalid window rule"]',False), ('null',False), ('{}',False), ('[false]',False), ('not JSON',False)]:
   with self.subTest(response=response), tempfile.TemporaryDirectory() as tmp:
    home=Path(tmp)/'home';home.mkdir();bin_dir=Path(tmp)/'bin';bin_dir.mkdir()
    config=home/'.config/hypr/hyprland.lua';config.parent.mkdir(parents=True);config.write_text('-- preserve original\n')
    command=bin_dir/'hyprctl'
    command.write_text('#!/usr/bin/env python3\nimport os,sys\nif sys.argv[1:]==["-j","configerrors"]:print(os.environ["PRISM_TEST_RESPONSE"])\n')
    command.chmod(0o755)
    env=dict(os.environ,HOME=str(home),PATH=str(bin_dir)+os.pathsep+os.environ['PATH'],HYPRLAND_INSTANCE_SIGNATURE='prism-test',PRISM_TEST_RESPONSE=response)
    result=subprocess.run(['python3','-B',str(ROOT/'package.py'),'install'],env=env,capture_output=True,text=True)
    self.assertEqual(result.returncode==0,success,result.stderr)
    launcher=home/'.local/bin/omarchy-prism'
    self.assertEqual(launcher.exists(),success)
    if success:
     self.assertIn('require("hypr.prism")',config.read_text())
     self.assertTrue(os.access(launcher,os.X_OK))
    else:
     self.assertEqual(config.read_text(),'-- preserve original\n')
     self.assertFalse((home/'.config/hypr/prism.lua').exists())
 def test_layout_preserves_widget_options_and_deduplicates(self):
  spec=importlib.util.spec_from_file_location('prism_package',ROOT/'package.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  original={'bar':{'layout':{'left':[{'id':'kylen.menu'},{'id':'kylen.files-dolphin'},{'id':'clock'}],'right':[{'id':'sound'},{'id':'kylen.prism','settings':{'animate':False}},{'id':'kylen.prism-terminal','settings':{'custom':True}}]}}}
  result=module.shell_layout(original)
  self.assertEqual([r['id'] for r in result['bar']['layout']['left']],['kylen.menu','kylen.files-dolphin','kylen.prism','kylen.prism-terminal','kylen.prism-chrome','clock'])
  self.assertEqual(result['bar']['layout']['left'][2]['settings'],{'animate':False})
  self.assertEqual(result['bar']['layout']['left'][3]['settings'],{'custom':True})
  self.assertEqual(result['bar']['layout']['right'],[{'id':'sound'}])
  self.assertEqual(module.shell_layout(result),result)
  self.assertEqual(len(original['bar']['layout']['right']),3)
 def test_launcher_group_places_terminal_fourth_without_losing_media(self):
  spec=importlib.util.spec_from_file_location('prism_layout_group',ROOT/'package.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  original={'bar':{'layout':{'left':[{'id':'kylen.menu'},{'id':'kylen.media-glass','custom':True},{'id':'kylen.files-dolphin','animate':False},{'id':'kylen.tidal-hires'}],'center':[],'right':[]}}}
  result=module.shell_layout(original)
  left=result['bar']['layout']['left']
  self.assertEqual([r['id'] for r in left[:5]],['kylen.menu','kylen.files-dolphin','kylen.prism','kylen.prism-terminal','kylen.prism-chrome'])
  self.assertEqual(left[1]['animate'],False)
  self.assertEqual(left[5:],original['bar']['layout']['left'][1:2]+original['bar']['layout']['left'][3:])
  self.assertEqual(module.shell_layout(result),result)
 def test_migration_scope(self):
  project=ROOT.parent
  if not (project/"migration/exporter-reviewed.py").is_file():self.skipTest("Standalone bundle: root migration engine tested in parent project")
  spec=importlib.util.spec_from_file_location('exporter',project/'migration/exporter-reviewed.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  self.assertIn('.local/share/omarchy-prism',module.USER_ROOTS)
  with tempfile.TemporaryDirectory() as tmp:
   home=Path(tmp);(home/'.local/bin').mkdir(parents=True);(home/'.local/bin/omarchy-prism').write_text('launcher');(home/'.local/bin/ky-prism').write_text('launcher')
   self.assertIn('.local/bin/omarchy-prism',module.user_executable_roots(home))
   self.assertIn('.local/bin/ky-prism',module.user_executable_roots(home))

 def test_media_group_follows_workspaces_and_preserves_options(self):
  spec=importlib.util.spec_from_file_location('prism_media_layout',ROOT/'package.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  original={'bar':{'layout':{'left':[{'id':'kylen.menu'},{'id':'kylen.media-glass','custom':42},{'id':'kylen.cava-spectrum'},{'id':'omarchy.workspaces'}],'center':[{'id':'omarchy.clock'}],'right':[{'id':'sound'}]}}}
  result=module.shell_layout(original)
  ids=[row['id'] for row in result['bar']['layout']['left']]
  self.assertEqual(ids[-3:],['omarchy.workspaces','kylen.media-glass','kylen.cava-spectrum'])
  self.assertEqual(result['bar']['layout']['left'][-2]['custom'],42)
  self.assertEqual(result['bar']['layout']['center'],original['bar']['layout']['center'])
  self.assertEqual(module.shell_layout(result),result)

class NewIntegration(unittest.TestCase):
 def test_dependency_plans(self):
  from unittest.mock import patch
  spec=importlib.util.spec_from_file_location('prism_deps',ROOT/'dependencies.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  for distro,expected in [('arch','pacman'),('ubuntu','apt-get'),('debian','apt-get')]:
   with self.subTest(distro=distro),patch.object(module.Path,'read_text',return_value='ID='+distro+'\n'):
    command=module.plan();self.assertEqual(command[0],expected)
    self.assertIn('udisks2',command)
    self.assertFalse(any('curl' in item or 'upgrade' in item for item in command))
 def test_chrome_helper_uses_arguments_without_shell(self):
  import time
  with tempfile.TemporaryDirectory() as tmp:
   directory=Path(tmp);record=directory/'arguments.json'
   browser=directory/'google-chrome-stable';browser.write_text('#!/usr/bin/python3\nimport json,os,sys\nfrom pathlib import Path\nPath(os.environ["PRISM_LAUNCH_RECORD"]).write_text(json.dumps(sys.argv[1:]))\n');browser.chmod(0o755)
   env=dict(os.environ,PATH=str(directory),PRISM_LAUNCH_RECORD=str(record))
   run=subprocess.run([sys.executable,str(ROOT/'omarchy-prism-chrome')],env=env,capture_output=True,text=True,timeout=5)
   self.assertEqual(run.returncode,0,run.stderr)
   until=time.monotonic()+3
   while not record.exists() and time.monotonic()<until:time.sleep(.01)
   self.assertEqual(json.loads(record.read_text()),['--new-window'])


 def test_optional_nvjpeg_artifact(self):
  spec=importlib.util.spec_from_file_location('nvjpeg_package',ROOT/'package.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  with tempfile.TemporaryDirectory() as tmp:
   module.ROOT=Path(tmp);build=module.ROOT/'build';build.mkdir()
   metadata=build/'nvjpeg-build.json';binary=build/'libprism_nvjpeg.so'
   metadata.write_text('{"enabled":false}');binary.write_bytes(b'stale')
   self.assertIsNone(module.nvjpeg_artifact())
   metadata.write_text('{"enabled":true}')
   self.assertEqual(module.nvjpeg_artifact(),binary)
   binary.unlink()
   with self.assertRaises(ValueError):module.nvjpeg_artifact()
   metadata.unlink();prebuilt=module.ROOT/'prebuilt';prebuilt.mkdir()
   bundled=prebuilt/'libprism_nvjpeg.so';bundled.write_bytes(b'bundled')
   self.assertEqual(module.nvjpeg_artifact(),bundled)

if __name__=='__main__':unittest.main()
