import hashlib
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WarpAssets(unittest.TestCase):
    def test_baked_shader_corresponds_to_reviewed_source(self):
        folder = ROOT / 'terminal-plugin'
        metadata = json.loads((folder / 'WarpGlow.meta.json').read_text())
        for name, sha in metadata['files'].items():
            self.assertEqual(hashlib.sha256((folder / name).read_bytes()).hexdigest(), sha)
        self.assertEqual(metadata['qsbVersion'], 64)


if __name__ == '__main__':
    unittest.main()
