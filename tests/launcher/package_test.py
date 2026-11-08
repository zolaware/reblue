"""Exercise production AppDir staging with a fake packer, without game/SDK dependencies."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASH = shutil.which('bash')


class PackageTests(unittest.TestCase):
    def test_each_packaged_platform_has_a_literal_program_match(self):
        profile = json.loads((ROOT / 'recomplauncher.json').read_text(encoding='utf-8'))
        for role in ('prepare', 'start'):
            for program in ('reblue.exe', 'reblue', 'reblue.AppImage', 'reblue.app/Contents/MacOS/reblue'):
                self.assertIn(program, profile[role]['program'])
            self.assertFalse(any('*' in name for name in profile[role]['program']))

    def test_three_disc_inputs_are_required_and_passed_as_separate_paths(self):
        profile = json.loads((ROOT / 'recomplauncher.json').read_text(encoding='utf-8'))
        self.assertNotIn('game', profile)
        self.assertEqual([i['id'] for i in profile['inputs'][:3]], ['disc1', 'disc2', 'disc3'])
        for item in profile['inputs'][:3]:
            self.assertEqual(item['role'], 'disc')
            self.assertFalse(item.get('optional', False))
            self.assertEqual(item['formats'], [{'type': 'file', 'extensions': ['iso']}])
            self.assertNotIn('destination', item)
        self.assertEqual(profile['prepare']['args'], [
            '--prepare-content', '--disc1', '{input:disc1}', '--disc2', '{input:disc2}',
            '--disc3', '{input:disc3}', '--install-dir', '{install}',
            '--dlc={input:dlc1}', '--dlc={input:dlc2}', '--dlc={input:dlc3}'])

    def test_dlc_inputs_are_optional_and_each_flag_contains_its_placeholder(self):
        profile = json.loads((ROOT / 'recomplauncher.json').read_text(encoding='utf-8'))
        dlc = profile['inputs'][3:]
        self.assertEqual([i['id'] for i in dlc], ['dlc1', 'dlc2', 'dlc3'])
        for item in dlc:
            self.assertEqual(item['role'], 'dlc')
            self.assertTrue(item['optional'])
            # STFS packages commonly have no extension.
            self.assertEqual(item['formats'], [{'type': 'file', 'extensions': []}])
            self.assertNotIn('destination', item)
            self.assertIn('--dlc={input:' + item['id'] + '}', profile['prepare']['args'])
        self.assertFalse(any('dlc' in a for a in profile['start']['args']))

    def package_fixture(self, image_name='reblue-1.3.1-x86_64.AppImage'):
        temporary = tempfile.TemporaryDirectory(prefix='reblue-package-')
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        build = root / 'out/build/fixture'
        build.mkdir(parents=True)
        program = build / 'reblue'
        program.write_text('#!/bin/sh\nprintf "%s\\0" "$@" > "$REBLUE_TEST_ARGS"\n', encoding='utf-8')
        program.chmod(0o755)
        (build / 'gamecontrollerdb.txt').write_text('fixture', encoding='utf-8')
        (build / 'librexruntime.so').write_bytes(b'fixture library')
        (root / 'res').mkdir()
        (root / 'res/reblue.png').write_bytes(b'fixture icon')
        (root / 'docs').mkdir()
        shutil.copy2(ROOT / 'recomplauncher.json', root / 'recomplauncher.json')
        shutil.copy2(ROOT / 'docs/RECOMPLAUNCHER.md', root / 'docs/RECOMPLAUNCHER.md')
        packer = root / 'packer'
        # A tar stands in for SquashFS here, so the test can inspect everything
        # handed to appimagetool. Actual embedded-reader/launch checks are separate.
        packer.write_text('''#!/usr/bin/env bash
set -euo pipefail
[ "$1" = --mksquashfs-opt=-no-xattrs ]
shift
[ "$#" -eq 2 ]
cp -a "$1" "$TEST_APPDIR"
tar -cf "$2" -C "$1" .
''', encoding='utf-8')
        packer.chmod(0o755)
        env = {**os.environ, 'PRESET': 'fixture', 'OUT_FILE': image_name,
               'APPIMAGE_ARCH': 'aarch64' if 'aarch64' in image_name else 'x86_64',
               'MAX_GLIBC': '2.39', 'MAX_GLIBCXX': '3.4.33',
               'PACKER': packer.as_posix(), 'TEST_APPDIR': (root / 'AppDir').as_posix(),
               'PACKAGING_SCRIPT': (ROOT / '.github/scripts/package-appimage.sh').as_posix()}
        # Only external ELF inspection/download tools are stubbed. All production
        # staging, AppRun generation and final packaging commands execute as written.
        subprocess.run([BASH, '-euc', '''
ldd() { :; }
objdump() { :; }
curl() { cp "$PACKER" "$3"; }
source "$PACKAGING_SCRIPT"
'''], cwd=root, check=True, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return root, root / 'dist' / image_name

    @unittest.skipUnless(sys.platform == 'linux' and BASH and shutil.which('tar'),
                         'AppImage staging requires Linux, bash and tar')
    def test_linux_appimage_embeds_profile_without_zip_or_sidecar(self):
        for architecture in ('x86_64', 'aarch64'):
            with self.subTest(architecture=architecture):
                root, image = self.package_fixture(f'reblue-1.3.1-{architecture}.AppImage')
                self.assertEqual(list((root / 'dist').iterdir()), [image])
                with tarfile.open(image) as archive:
                    members = {m.name.removeprefix('./'): m for m in archive.getmembers()}
                    manifest = members['recomplauncher.json']
                    self.assertTrue(manifest.isfile())
                    self.assertLessEqual(manifest.size, 1024 * 1024)
                    profile = json.load(archive.extractfile(manifest))
                    self.assertEqual(profile, json.loads((ROOT / 'recomplauncher.json').read_text()))
                    self.assertIn('usr/share/doc/reblue/RECOMPLAUNCHER.md', members)
                    self.assertIn('usr/bin/reblue', members)
                    self.assertFalse(any(name.startswith(('game/', 'profiles/', 'cache/')) for name in members))
                    self.assertNotIn('usr/bin/recomplauncher.json', members)

    @unittest.skipUnless(sys.platform == 'linux' and BASH,
                         'Linux AppRun uses GNU readlink')
    def test_apprun_preserves_prepare_and_launch_arguments(self):
        root, _ = self.package_fixture()
        profile = json.loads((ROOT / 'recomplauncher.json').read_text())
        output = root / 'arguments'
        for role in ('prepare', 'start'):
            with self.subTest(role=role):
                args = [a.replace('{game}', '/Disc images/First.ISO').replace('{install}', '/Managed game')
                        for a in profile[role]['args']]
                for i in range(1, 4):
                    args = [a.replace('{input:disc' + str(i) + '}', f'/Disc {i} folder α/DVD.ISO')
                            .replace('{input:dlc' + str(i) + '}', f'/DLC {i} folder α/Package') for a in args]
                subprocess.run([BASH, (root / 'AppDir/AppRun').as_posix(), *args], check=True,
                               env={**os.environ, 'REBLUE_TEST_ARGS': output.as_posix()})
                received = output.read_bytes().split(b'\0')[:-1]
                self.assertEqual(received, [a.encode('utf-8') for a in args])


if __name__ == '__main__':
    unittest.main()
