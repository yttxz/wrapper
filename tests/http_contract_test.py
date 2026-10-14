#!/usr/bin/env python3
"""Check the production HTTP handlers with a separately linked fixture backend."""
import base64
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

binary = sys.argv[1]
with socket.socket() as sock:
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
with tempfile.TemporaryDirectory() as data, tempfile.TemporaryFile() as logs:
    process = subprocess.Popen([binary, '--base-dir', data, '--host', '127.0.0.1', '--port', str(port)], stdout=logs, stderr=logs)
    base = f'http://127.0.0.1:{port}'
    def fetch(path, body=None):
        req = urllib.request.Request(base + path, data=None if body is None else json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
        try:
            response = urllib.request.urlopen(req, timeout=3)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            return json.load(response)
    try:
        for _ in range(100):
            if process.poll() is not None:
                raise RuntimeError('fixture HTTP server exited')
            try:
                assert fetch('/status') == {'code': 0, 'msg': 'SUCCESS', 'data': {'regions': ['us']}}
                break
            except OSError:
                time.sleep(.05)
        else:
            raise RuntimeError('fixture HTTP server did not start')
        assert fetch('/m3u8?adamId=123')['data']['m3u8'].endswith('.m3u8')
        key = fetch('/key?adamId=123&uri=skd%3A%2F%2Fexample%2Ftrack')['data']
        assert len(base64.b64decode(key['ctx'])) == 0x8000
        assert len(base64.b64decode(key['state'])) == 0x2100
        assert all(key[name] == '0x1' for name in ('rcx', 'rax', 'rdx', 'r9', 'rbp'))
        assert fetch('/lyrics?adamId=123&syllable=1')['data']['lyrics'] == '<tt>fixture lyrics</tt>'
        assert fetch('/webplayback?adamId=123')['data']['m3u8'].endswith('.m3u8')
        for drm in ('wv', 'pr'):
            result = fetch('/license', {'adamId': '123', 'challenge': 'fixture', 'uri': 'fixture', 'drm-type': drm})
            assert base64.b64decode(result['data']['license']) == b'license'
        for path in ('/m3u8', '/key?adamId=123', '/key?adamId=x&uri=skd://x', '/key?adamId=123&uri=https://x',
                     '/key?adamId=123&uri=skd://itunes.apple.com/P000000000/s1/e1', '/lyrics', '/webplayback'):
            assert fetch(path)['code'] == 400, path
        assert fetch('/license', {})['code'] == 400
        assert fetch('/license', {'adamId': '123', 'challenge': 'x', 'uri': 'x', 'drm-type': 'invalid'})['code'] == 400
        assert fetch('/absent')['code'] == 404
        # Optional external client probe uses the local downloader snapshot.
        if len(sys.argv) > 2:
            subprocess.run([sys.argv[2], base], check=True)
        print('HTTP handler contracts passed (fixture backend; no Apple authentication)')
    finally:
        process.terminate()
        process.wait(timeout=5)
