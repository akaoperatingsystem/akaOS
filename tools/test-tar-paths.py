#!/usr/bin/env python3
# Copyright (c) 2026 akaOS Authors
# SPDX-License-Identifier: BSD-3-Clause
"""Host tests of tar's actual extraction loop (akaOS-only creation is stubbed)."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def archive(entries):
    data = b''
    for name, payload, kind in entries:
        header = bytearray(512)
        name = os.fsencode(name)
        assert len(name) <= 100
        header[:len(name)] = name
        header[124:136] = f'{len(payload):011o}\0'.encode()
        header[156] = ord(kind)
        header[257:263] = b'ustar\0'
        header[148:156] = b'        '
        header[148:156] = f'{sum(header):06o}\0 '.encode()
        data += header + payload + b'\0' * (-len(payload) % 512)
    return data + b'\0' * 1024


class ExtractionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.binary = str(Path(cls.build.name) / 'tar')
        source = (ROOT / 'userspace/bin/tar/main.c').read_text()
        start = source.index('static int write_header(')
        end = source.index('/* Resolve each parent', start)
        source = source[:start] + '''static int archive_entry(int fd, const char *path) {
            (void)fd; (void)path; return -1;
        }
        ''' + source[end:]
        path = Path(cls.build.name) / 'tar.c'
        path.write_text(source)
        subprocess.run(['cc', '-Wall', '-Wextra', str(path), '-o', cls.binary], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_containment(self):
        for use_file in (False, True):
            with self.subTest(use_file=use_file), tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                root = base / 'root'
                root.mkdir()
                outside = base / 'outside'
                outside.mkdir()
                victim = outside / 'victim'
                victim.write_bytes(b'original')
                (root / 'link').symlink_to(outside, target_is_directory=True)
                (root / 'leaf').symlink_to(victim)
                data = archive([
                    ('../outside/victim', b'bad', '0'),
                    (str(victim), b'bad', '0'),
                    ('new/../../outside/dir', b'', '5'),
                    ('link/subdir/file', b'bad', '0'),
                    ('link/', b'', '5'),
                    ('leaf', b'bad', '0'),
                    ('./safe//dir/', b'', '5'),
                    ('safe/dir/file', b'good', '0'),
                ])
                args = [self.binary, '-x', '-C', str(root)]
                if use_file:
                    arc = base / 'input.tar'
                    arc.write_bytes(data)
                    args += ['-f', str(arc)]
                run = subprocess.run(args, input=data, capture_output=True)
                self.assertEqual(run.returncode, 1, run.stderr)
                self.assertEqual(victim.read_bytes(), b'original')
                self.assertEqual(list(outside.iterdir()), [victim])
                self.assertFalse((root / 'new').exists())
                self.assertEqual((root / 'safe/dir/file').read_bytes(), b'good')

    def test_normal_and_list(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            entries = [('./', b'', '5'), ('nested/file', b'first', '0'),
                       ('nested/file', b'last', '0'), ('n' * 100, b'full', '0')]
            data = archive(entries)
            run = subprocess.run([self.binary, '-t'], cwd=root, input=data, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(run.stdout, ''.join(e[0] + '\n' for e in entries).encode())
            self.assertEqual(list(root.iterdir()), [])
            run = subprocess.run([self.binary, '-x'], cwd=root, input=data, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual((root / 'nested/file').read_bytes(), b'last')
            self.assertEqual((root / ('n' * 100)).read_bytes(), b'full')


if __name__ == '__main__':
    unittest.main()
