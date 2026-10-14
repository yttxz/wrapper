#!/usr/bin/env python3
"""Exercise real Android libcurl against a disposable local TLS server."""
import http.server
import ipaddress
import os
import platform
import ssl
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

image, probe = sys.argv[1:3]

def docker(*args, **kwargs):
    return subprocess.run(['docker', *args], check=True, **kwargs)

if platform.system() == 'Darwin':
    output = docker('run', '--platform', 'linux/amd64', '--rm', '--entrypoint', '/bin/sh', image,
                    '-c', 'getent ahostsv4 host.docker.internal | head -1', capture_output=True, text=True).stdout
    host = output.split()[0]
else:
    host = docker('network', 'inspect', 'bridge', '--format', '{{(index .IPAM.Config 0).Gateway}}', capture_output=True, text=True).stdout.strip()
ipaddress.ip_address(host)
with tempfile.TemporaryDirectory() as temp:
    directory = Path(temp)
    cert, key = directory/'cert.pem', directory/'key.pem'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                    '-subj', '/CN=wrapper TLS regression', '-addext', f'subjectAltName=IP:{host}',
                    '-keyout', str(key), '-out', str(cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200); self.end_headers(); self.wfile.write(b'ok')
        def log_message(self, *args): pass
    server = http.server.HTTPServer(('0.0.0.0', 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    url = f'https://{host}:{server.server_port}/'
    args = ['run', '--platform', 'linux/amd64', '--rm', '--entrypoint', '/bin/sh',
            '-v', str(Path(probe).resolve())+':/app/rootfs/test/tls-probe:ro',
            '-v', str(directory.resolve())+':/app/rootfs/test/certs:ro']
    command = 'LD_LIBRARY_PATH=/system/lib64 chroot /app/rootfs /test/tls-probe "$1"'
    try:
        failed = subprocess.run(['docker', *args, image, '-c', command, 'probe', url], capture_output=True)
        assert failed.returncode == 1, 'untrusted certificate was accepted or probe failed unexpectedly'
        docker(*args, '-e', 'WRAPPER_CA_BUNDLE=/test/certs/cert.pem', image,
               '-c', command, 'probe', url, stdout=subprocess.DEVNULL)
        print('Android libcurl rejected an untrusted certificate and accepted the explicitly trusted CA')
    finally:
        server.shutdown(); thread.join(timeout=2)
