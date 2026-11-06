"""Validate release-local program resolution, including the production Linux zip step."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


class PackageTests(unittest.TestCase):
    def test_each_packaged_platform_has_a_literal_program_match(self):
        profile = json.loads((ROOT / 'recomplauncher.json').read_text(encoding='utf-8'))
        for role in ('prepare', 'start'):
            for program in ('reblue.exe', 'reblue', 'reblue.AppImage', 'reblue.app/Contents/MacOS/reblue'):
                self.assertIn(program, profile[role]['program'])
            self.assertFalse(any('*' in name for name in profile[role]['program']))

    @unittest.skipUnless(os.name == 'posix' and shutil.which('bash') and shutil.which('zip'),
                         'Linux packaging check requires bash and zip')
    def test_linux_zip_has_a_resolvable_prepare_program_and_no_user_data(self):
        script = (ROOT / '.github/scripts/package-appimage.sh').read_text(encoding='utf-8')
        packaging = script[script.index('# RecompLauncher reads a release-local manifest'):]
        with tempfile.TemporaryDirectory(prefix='reblue-package-') as temporary:
            root = Path(temporary)
            (root / 'dist').mkdir()
            (root / 'docs').mkdir()
            staging = root / 'staging'
            staging.mkdir()
            image = root / 'dist/reblue-1.3.1-x86_64.AppImage'
            image.write_bytes(b'test AppImage')
            shutil.copy2(ROOT / 'recomplauncher.json', root / 'recomplauncher.json')
            shutil.copy2(ROOT / 'docs/RECOMPLAUNCHER.md', root / 'docs/RECOMPLAUNCHER.md')
            subprocess.run(['bash', '-euc', packaging], cwd=root, check=True,
                           env={**os.environ, 'STAGING': str(staging), 'OUT_FILE': image.name},
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            with zipfile.ZipFile(root / 'dist/reblue-1.3.1-x86_64.zip') as archive:
                names = set(archive.namelist())
                profile = json.loads(archive.read('recomplauncher.json'))
                self.assertTrue(any(name in names for name in profile['prepare']['program']))
                self.assertTrue(any(name in names for name in profile['start']['program']))
                self.assertEqual(archive.read('reblue.AppImage'), b'test AppImage')
                self.assertIn('docs/RECOMPLAUNCHER.md', names)
                self.assertFalse(any(name.startswith(('game/', 'profiles/', 'cache/')) for name in names))


if __name__ == '__main__':
    unittest.main()
