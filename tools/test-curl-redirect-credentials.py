#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise production curl against local redirect servers and inspect requests."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
requests = []
routes = {}
redirect_status = 302


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def do_GET(self):
        requests.append((self.path, self.headers))
        target = routes.get(self.path)
        self.send_response(redirect_status if target else 200)
        if target:
            self.send_header('Location', target)
        self.send_header('Content-Length', '0')
        self.send_header('Connection', 'close')
        self.end_headers()

    def log_message(self, *args):
        pass


servers = [ThreadingHTTPServer(('127.0.0.1', 0), Handler) for _ in range(2)]
for server in servers:
    threading.Thread(target=server.serve_forever, daemon=True).start()

try:
    with tempfile.TemporaryDirectory() as directory:
        binary = Path(directory) / 'curl'
        subprocess.run(['cc', '-std=c17', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra',
                        '-Werror', str(ROOT / 'userspace/bin/curl/main.c'),
                        '-o', str(binary)], check=True)
        origin = f'http://localhost:{servers[0].server_port}'
        other_port = f'http://localhost:{servers[1].server_port}'
        other_host = f'http://127.0.0.1:{servers[0].server_port}'
        other_scheme = f'https://localhost:{servers[0].server_port}'

        def check(targets, expected, custom=False):
            requests.clear()
            routes.clear()
            for index, target in enumerate(targets):
                routes[f'/step{index}'] = f'{target}/step{index + 1}'
            credentials = (['-H', 'aUtHoRiZaTiOn: Basic dXNlcjpwYXNz',
                            '-H', 'cOoKiE: session=secret'] if custom else
                           ['-u', 'user:pass', '-b', 'session=secret'])
            subprocess.run([str(binary), '-s', '-L', '-k', *credentials,
                            '-H', 'X-Test: retained', origin + '/step0'],
                           check=True, timeout=10, capture_output=True)
            assert len(requests) == len(expected), requests
            for (_, headers), allowed in zip(requests, expected):
                assert headers.get_all('Authorization') == (
                    ['Basic dXNlcjpwYXNz'] if allowed else None), requests
                assert headers.get_all('Cookie') == (
                    ['session=secret'] if allowed else None), requests
                assert headers['X-Test'] == 'retained'

        for redirect_status in (301, 302, 303, 307, 308):
            for custom in (False, True):
                check([], [True], custom)
                check([origin], [True, True], custom)
                check([origin.upper().replace('HTTP://', 'http://')], [True, True], custom)
                for destination in (other_port, other_host, other_scheme):
                    check([destination], [True, False], custom)
                check([other_port, other_port, origin], [True, False, False, True], custom)
finally:
    for server in servers:
        server.shutdown()
        server.server_close()

print('curl redirect credential regressions passed (70 flows)')
