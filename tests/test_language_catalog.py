import ast
import importlib.util
import json
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]


class LanguageCatalog(unittest.TestCase):
    def test_reviewed_catalog_has_matching_placeholders_and_generated_header(self):
        spec=importlib.util.spec_from_file_location('prism_catalog_codegen',ROOT/'i18n/generate.py')
        app=importlib.util.module_from_spec(spec);spec.loader.exec_module(app)
        self.assertEqual(app.canonical_content((ROOT/'src/language_catalog.h').read_text()),app.generated())
        pairs=json.loads((ROOT/'i18n/en.json').read_text(),object_pairs_hook=list)
        self.assertEqual(len(pairs),len(dict(pairs)))
        for source,target in pairs:
            self.assertTrue(target.strip(),source)
            self.assertEqual(sorted(re.findall(r'%\d+',source)),sorted(re.findall(r'%\d+',target)),source)
            self.assertFalse(re.search('[ąćęłńóśżźĄĆĘŁŃÓŚŻŹ]',target),target)

    def test_all_authored_polish_strings_are_in_the_catalog(self):
        catalog=json.loads((ROOT/'i18n/en.json').read_text())
        pattern=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|R"(?P<d>[^\s()\\]{0,16})\([\s\S]*?\)(?P=d)"|\'(?:\\.|[^\'\\])*\'|"(?:\\.|[^"\\])*"')
        missing=[]
        for path in sorted([*(ROOT/'src').iterdir(),*(ROOT/'src/gtk').iterdir()]):
            if path.suffix not in ('.cpp','.h') or path.name in ('language.cpp','language_catalog.h'):continue
            text=path.read_text();strings=[]
            for token in pattern.finditer(text):
                if not token.group().startswith('"'):continue
                try:value=ast.literal_eval(token.group())
                except (ValueError,SyntaxError):continue
                if strings and not text[strings[-1][1]:token.start()].strip():
                    old=strings.pop();strings.append((old[0],token.end(),old[2]+value))
                else:strings.append((token.start(),token.end(),value))
            for start,end,value in strings:
                if path.name in ('localized_widgets.h','dialogs.cpp') and value in (
                    ' plików • Ctrl/Shift w bibliotece: zaznacz wiele',' dalszych plików'):
                    continue # Structural matchers for the two catalogued file-list templates.
                if re.search('[ąćęłńóśżźĄĆĘŁŃÓŚŻŹ]',value) and value not in catalog:
                    missing.append(str(path.name)+': '+value)
        self.assertEqual(missing,[])


if __name__=='__main__':unittest.main()
