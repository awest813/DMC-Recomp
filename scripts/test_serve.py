"""Local integration checks for launcher server identity and disc streaming."""
import json
import pathlib
import socket
import subprocess
import sys
import time
import urllib.request
import urllib.error
import urllib.parse

root = pathlib.Path(__file__).resolve().parents[1]
fixture = root / 'build' / 'server-test'
fixture.mkdir(parents=True, exist_ok=True)
disc = fixture / 'fixture.iso'
disc.write_bytes(b'0123456789')
(fixture / 'ps2web.html').write_text('fixture', encoding='utf-8')
with socket.socket() as sock:
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
server = subprocess.Popen([sys.executable, str(root / 'scripts/serve.py'), str(fixture), str(disc), str(port)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
base = f'http://127.0.0.1:{port}'
try:
    for attempt in range(50):
        try:
            with urllib.request.urlopen(base + '/__ps2web_status', timeout=1) as response:
                identity = json.load(response)
            break
        except OSError:
            time.sleep(0.1)
    else:
        raise AssertionError('server did not start')
    assert identity == {'application': 'ps2-web-recomp', 'build': str(fixture), 'disc': str(disc)}
    request = urllib.request.Request(base + '/disc.iso', headers={'Range': 'bytes=2-5'})
    with urllib.request.urlopen(request, timeout=2) as response:
        assert response.status == 206
        assert response.read() == b'2345'
        assert response.headers['Cross-Origin-Opener-Policy'] == 'same-origin'
        assert response.headers['Cross-Origin-Embedder-Policy'] == 'require-corp'
    with urllib.request.urlopen(base + '/', timeout=2) as response:
        assert response.read() == b'fixture'
    (fixture / 'file with spaces.txt').write_text('spaces', encoding='utf-8')
    with urllib.request.urlopen(base + '/file%20with%20spaces.txt', timeout=2) as response:
        assert response.read() == b'spaces'
    for byte_range in ('bytes=20-30', 'bytes=8-3'):
        try:
            urllib.request.urlopen(urllib.request.Request(base + '/disc.iso', headers={'Range': byte_range}), timeout=2)
            raise AssertionError('invalid range succeeded')
        except urllib.error.HTTPError as response:
            assert response.code == 416
            assert response.headers['Content-Range'] == 'bytes */10'
            assert response.read() == b''
    outside = fixture.parent / 'server-outside.txt'
    outside.write_text('outside build', encoding='utf-8')
    for path in ('/../server-outside.txt', '/%2e%2e/server-outside.txt', '/' + urllib.parse.quote(str(outside), safe='')):
        try:
            urllib.request.urlopen(base + path, timeout=2)
            raise AssertionError('served a file outside the build directory')
        except urllib.error.HTTPError as response:
            assert response.code == 404
    print('Server: identity, ranges, path containment, escaped filenames and isolation headers passed')
finally:
    server.terminate()
    server.wait(timeout=5)
