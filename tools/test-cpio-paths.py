#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Host regression tests for cpio extraction containment."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def archive(fmt, entries):
    result = b''
    for name, data, mode in entries + [('TRAILER!!!', b'', 0)]:
        name = os.fsencode(name) + b'\0'
        if fmt == 'bin':
            header = struct.pack('<13H', 0o70707, 0, 0, mode, 0, 0, 1,
                                 0, 0, 1234, len(name), 0, len(data))
            alignment = 2
        else:
            fields = (0, mode, 0, 0, 1, 1234, len(data), 0, 0, 0, 0,
                      len(name), sum(data) if fmt == 'crc' else 0)
            header = (('070702' if fmt == 'crc' else '070701') +
                      ''.join(f'{value:08x}' for value in fields)).encode()
            alignment = 4
        result += header + name
        result += b'\0' * (-len(result) % alignment)
        result += data + b'\0' * (-len(data) % alignment)
    return result


class ExtractionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.binary = str(Path(cls.build.name) / 'cpio')
        subprocess.run(['cc', '-Wall', '-Wextra', str(ROOT / 'userspace/bin/cpio/main.c'),
                        '-o', cls.binary], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_containment(self):
        for fmt in ('newc', 'crc', 'bin'):
            with self.subTest(fmt=fmt), tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                root = base / 'root'
                root.mkdir()
                outside = base / 'outside'
                outside.mkdir()
                victim = outside / 'victim'
                victim.write_bytes(b'original')
                os.utime(victim, (5000, 5000))
                (root / 'link').symlink_to(outside, target_is_directory=True)
                (root / 'leaf').symlink_to(victim)
                entries = [('../outside/victim', b'bad', 0o100644),
                           (str(victim), b'bad', 0o100644),
                           ('link/victim', b'bad', 0o100644),
                           ('leaf', b'bad', 0o100644),
                           ('new/../../outside/dir', b'', 0o40755),
                           ('link/subdir/file', b'bad', 0o100644),
                           ('./safe//dir', b'', 0o40755),
                           ('safe/dir/file', b'good', 0o100644)]
                run = subprocess.run([self.binary, '-idum', '--quiet'], cwd=root,
                                     input=archive(fmt, entries), capture_output=True)
                self.assertEqual(run.returncode, 1, run.stderr)
                self.assertEqual(victim.read_bytes(), b'original')
                self.assertEqual(victim.stat().st_mtime, 5000)
                self.assertEqual(sorted(p.name for p in outside.iterdir()), ['victim'])
                self.assertFalse((root / 'new').exists())
                self.assertEqual((root / 'safe/dir/file').read_bytes(), b'good')
                self.assertEqual((root / 'safe/dir/file').stat().st_mtime, 1234)

    def test_modes(self):
        for fmt in ('newc', 'crc', 'bin'):
            with self.subTest(fmt=fmt), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                data = archive(fmt, [('missing/file', b'hello', 0o100644)])
                def run(*args):
                    return subprocess.run([self.binary, *args, '--quiet'], cwd=root,
                                          input=data, capture_output=True)
                self.assertEqual(run('-it').stdout, b'missing/file\n')
                self.assertEqual(run('-id', '--to-stdout').stdout, b'hello')
                self.assertEqual(list(root.iterdir()), [])
                self.assertEqual(run('-i').returncode, 1)
                self.assertEqual(run('-id').returncode, 0)
                self.assertEqual(run('-i').returncode, 1)
                self.assertEqual(run('-iu').returncode, 0)
                self.assertEqual((root / 'missing/file').read_bytes(), b'hello')

    def test_many_patterns(self):
        for count in (1, 31, 32, 33, 64, 1024):
            with self.subTest(count=count):
                patterns = ['missing'] * (count - 1) + ['selected*']
                data = archive('newc', [('ignored', b'', 0o100644),
                                        ('selected-file', b'hello', 0o100644)])
                for flags, expected in (('-it', b'selected-file\n'),
                                        ('-itf', b'ignored\n')):
                    run = subprocess.run([self.binary, flags, '--quiet', *patterns],
                                         input=data, capture_output=True)
                    self.assertEqual(run.returncode, 0, run.stderr)
                    self.assertEqual(run.stdout, expected)


if __name__ == '__main__':
    unittest.main()
