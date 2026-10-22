"""Window download engine against a local range server that models a PS5 link.

Each server connection is capped at a fixed rate, as a PS5 connection is on a
long reordering path, so total speed only grows with more connections. Faults
are injected on demand. Runs the real ORBIT_TEST binary; no console needed.

    python3 tests/window_integration.py [-k name]
Environment: ORBIT_BIN (default build/orbit-test), ORBIT_LEGACY_BIN (optional
binary of the previous engine for the saved-partial upgrade test).
"""
import hashlib
import http.server
import json
import os
import random
import re
import shutil
import signal
import socket
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get('ORBIT_BIN', os.path.join(ROOT, 'build/orbit-test'))
LEGACY_BIN = os.environ.get('ORBIT_LEGACY_BIN')
SIZE = int(os.environ.get('WINDOW_TEST_MIB', '192')) * 1024 * 1024
RATE = int(os.environ.get('WINDOW_TEST_RATE', str(256 * 1024)))  # bytes/s per connection
FILENAME = 'fixture.bin'
ETAG = '"orbit-window-fixture"'
WORK = tempfile.mkdtemp(prefix='orbit-window-')
SOURCE = os.path.join(WORK, FILENAME)
with open(SOURCE, 'wb') as out:
    rng = random.Random(1234)
    for _ in range(SIZE // (1 << 20)):
        out.write(rng.randbytes(1 << 20))
with open(SOURCE, 'rb') as f:
    DIGEST = hashlib.sha256(f.read()).hexdigest()


class Fixture(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    lock = threading.Lock()

    @classmethod
    def reset(cls):
        cls.fail500 = cls.drop = cls.stall = 0.0
        cls.stall_seconds = 40
        cls.throttle = (0, 0)  # (start, end) wall time
        cls.overload_at = 0  # active connections at which the host answers 500
        cls.active = cls.peak = 0
        cls.requests = {'redirect': 0, 'node': 0}
        cls.served = {'429': 0, '500': 0, 'drop': 0, 'stall': 0}
        cls.rng = random.Random(99)

    def log_message(self, *args):
        pass

    def headers_out(self, code, length, extra=()):
        self.send_response(code)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('ETag', ETAG)
        self.send_header('Last-Modified', 'Mon, 01 Jan 2024 00:00:00 GMT')
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Length', str(length))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()

    def empty(self, code, extra=()):
        self.send_response(code)
        self.send_header('Content-Length', '0')
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()

    def route(self):
        if self.path == '/redirect/' + FILENAME:
            with Fixture.lock:
                Fixture.requests['redirect'] += 1
            self.empty(302, [('Location', '/node/' + FILENAME)])
            return None
        if self.path != '/node/' + FILENAME:
            self.empty(404)
            return None
        with Fixture.lock:
            Fixture.requests['node'] += 1
        return True

    def do_HEAD(self):
        if self.route():
            self.headers_out(200, SIZE)

    def do_GET(self):
        if not self.route():
            return
        now = time.time()
        start, end = Fixture.throttle
        with Fixture.lock:
            r = Fixture.rng.random()
            throttled = start <= now < end
            overloaded = bool(Fixture.overload_at) and Fixture.active >= Fixture.overload_at
            if overloaded:
                Fixture.served['500'] += 1
            if throttled:
                Fixture.served['429'] += 1
            elif r < Fixture.fail500:
                Fixture.served['500'] += 1
        if throttled:
            return self.empty(429, [('Retry-After', '2')])
        if r < Fixture.fail500 or overloaded:
            return self.empty(500)
        match = re.fullmatch(r'bytes=(\d+)-(\d+)', self.headers.get('Range', ''))
        if self.headers.get('If-Match', ETAG) != ETAG:
            return self.empty(412)
        if not match:
            first, last, code = 0, SIZE - 1, 200
        else:
            first, last, code = int(match[1]), min(int(match[2]), SIZE - 1), 206
        length = last - first + 1
        extra = [('Content-Range', f'bytes {first}-{last}/{SIZE}')] if code == 206 else []
        self.headers_out(code, length, extra)
        with Fixture.lock:
            Fixture.active += 1
            Fixture.peak = max(Fixture.peak, Fixture.active)
            fault = Fixture.rng.random()
        try:
            with open(SOURCE, 'rb') as f:
                f.seek(first)
                sent, began = 0, time.monotonic()
                cut = Fixture.rng.randint(1, length) if fault < Fixture.drop + Fixture.stall else None
                while sent < length:
                    block = f.read(min(16384, length - sent))
                    if cut is not None and sent + len(block) >= cut:
                        if fault < Fixture.drop:
                            Fixture.served['drop'] += 1
                        else:
                            Fixture.served['stall'] += 1
                            time.sleep(Fixture.stall_seconds)
                        self.close_connection = True
                        self.connection.shutdown(socket.SHUT_RDWR)
                        return
                    self.wfile.write(block)
                    sent += len(block)
                    delay = began + sent / RATE - time.monotonic()
                    if delay > 0:
                        time.sleep(delay)
        except OSError:
            self.close_connection = True
        finally:
            with Fixture.lock:
                Fixture.active -= 1


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 256


class Orbit:
    def __init__(self, binary, state, drive, port, env=()):
        self.port = port
        environment = dict(os.environ, ORBIT_TEST_URL=f'{SERVER_URL}/redirect/{FILENAME}',
                           ORBIT_TEST_SIZE=str(SIZE), ORBIT_TEST_SHA256=DIGEST,
                           ORBIT_TEST_SOURCE='archive', **dict(env))
        self.process = subprocess.Popen([binary, '--state', state, '--storage', drive, '--port', str(port)],
                                        env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        code = None
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline and code is None:
            line = self.process.stdout.readline()
            found = re.search(r'Pairing code: (\d{6})', line)
            code = found and found[1]
        threading.Thread(target=self.process.stdout.read, daemon=True).start()
        assert code, 'Orbit did not start'
        self.token = None
        # The pairing code is printed before the API starts listening.
        for _ in range(100):
            try:
                self.token = self.call('/pair', {'code': code})['token']
                break
            except urllib.error.URLError:
                time.sleep(0.1)
        assert self.token, 'Orbit API did not start'

    def call(self, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        headers = {'Content-Type': 'application/json'}
        if self.token:
            headers['Authorization'] = 'Bearer ' + self.token
        request = urllib.request.Request(f'http://127.0.0.1:{self.port}/api/v1{path}', data=data, headers=headers)
        with urllib.request.urlopen(request, timeout=20) as response:
            return json.loads(response.read() or b'null')

    def stop(self):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(20)
            except subprocess.TimeoutExpired:
                self.process.kill()


class WindowTests(unittest.TestCase):
    def setUp(self):
        Fixture.reset()
        self.dir = tempfile.mkdtemp(dir=WORK)
        self.state = os.path.join(self.dir, 'state')
        self.drive = os.path.join(self.dir, 'drive')
        os.makedirs(self.drive)
        self.orbit = None
        self.connections = []
        self.statuses = set()

    def tearDown(self):
        if self.orbit:
            self.orbit.stop()
        shutil.rmtree(self.dir, ignore_errors=True)

    def start(self, binary=BIN, **env):
        if self.orbit:
            self.orbit.stop()
        self.orbit = Orbit(binary, self.state, self.drive, free_port(), env.items())
        return self.orbit

    def create(self):
        self.orbit.call('/sources', {'enabled': ['archive'], 'noticeVersion': 1, 'acknowledged': True})
        release = self.orbit.call('/catalog')[0]['id']
        storage = self.orbit.call('/storage')[0]['id']
        return self.orbit.call('/downloads', {'releaseId': release, 'storageId': storage, 'delivery': 'direct'})

    def job(self):
        return self.orbit.call('/downloads')[0]

    def wait(self, predicate, timeout=300):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            job = self.job()
            self.statuses.add(job['status'])
            for event in self.orbit.call('/diagnostics').get('events', []):
                if event.get('connections'):
                    self.connections.append(event['connections'])
            if predicate(job):
                return job
            self.assertNotEqual(job['status'], 'error', job)
            time.sleep(0.5)
        self.fail(f'timed out: {self.job()}')

    def assert_complete(self):
        job = self.wait(lambda j: j['status'] == 'complete')
        path = os.path.join(self.drive, 'homebrew', job['filename'] if 'filename' in job else FILENAME)
        if not os.path.exists(path):
            path = next(os.path.join(d, f) for d, _, fs in os.walk(self.drive) for f in fs if not f.endswith('.part'))
        with open(path, 'rb') as f:
            self.assertEqual(hashlib.sha256(f.read()).hexdigest(), DIGEST)
        return job

    def test_grows_connections_and_writes_exact_file(self):
        self.start()
        began = time.monotonic()
        self.create()
        self.assert_complete()
        elapsed = time.monotonic() - began
        print(f'\n  clean: {SIZE / elapsed / 1e6:.1f} MB/s, server peak {Fixture.peak} connections, '
              f'requests {Fixture.requests}')
        self.assertGreater(Fixture.peak, 16, 'never grew beyond the starting 16 connections')
        self.assertGreater(Fixture.requests['node'], 4 * Fixture.requests['redirect'],
                           'later requests should skip the redirect')

    def test_recovers_from_errors_drops_and_silent_stalls(self):
        Fixture.fail500, Fixture.drop, Fixture.stall = 0.04, 0.03, 0.01
        self.start()
        self.create()
        self.assert_complete()
        print(f'\n  faults served: {Fixture.served}')
        self.assertTrue(Fixture.served['500'] and Fixture.served['drop'] and Fixture.served['stall'], Fixture.served)

    def test_throttling_reduces_connections_and_finishes(self):
        self.start()
        self.create()
        self.wait(lambda j: j['received'] > SIZE // 8)
        Fixture.throttle = (time.time(), time.time() + 6)
        self.assert_complete()
        print(f'\n  429 served: {Fixture.served["429"]}, connections seen: {sorted(set(self.connections))}')
        self.assertGreater(Fixture.served['429'], 0)
        self.assertTrue(any(1 < c < 16 for c in self.connections), 'throttling did not reduce connections')

    def test_overloaded_host_backs_off_without_restarting_the_job(self):
        Fixture.overload_at = 28
        self.start()
        self.create()
        self.assert_complete()
        print(f'\n  overload 500s: {Fixture.served["500"]}, peak {Fixture.peak}, statuses {sorted(self.statuses)}')
        self.assertGreater(Fixture.served['500'], 0, 'the engine never reached the host limit')
        self.assertNotIn('retrying', self.statuses, 'overload must be absorbed by the engine')

    def test_pause_and_resume_keep_one_exact_file(self):
        self.start()
        self.create()
        job = self.wait(lambda j: j['received'] > SIZE // 3)
        self.orbit.call(f'/downloads/{job["id"]}/pause', {})
        paused = self.wait(lambda j: j['status'] == 'paused')
        self.start()  # a restarted service resumes from the saved cursor
        self.assertGreaterEqual(self.job()['received'], paused['received'] - 8 * 1024 * 1024)
        self.orbit.call(f'/downloads/{job["id"]}/resume', {})
        self.assert_complete()

    @unittest.skipUnless(LEGACY_BIN, 'ORBIT_LEGACY_BIN not set')
    def test_partial_saved_by_previous_engine_finishes(self):
        self.start(LEGACY_BIN)
        self.create()
        job = self.wait(lambda j: j['received'] > SIZE // 5)
        self.orbit.stop()
        self.orbit = None
        with open(os.path.join(self.state, 'state.json')) as f:
            saved = json.load(f)['jobs'][0]
        ranges = saved['rangeReceived']
        print(f'\n  previous engine saved {len(ranges)} ranges: {[r >> 20 for r in ranges]} MiB')
        self.assertEqual(len(ranges), 8)
        self.assertGreater(sum(1 for r in ranges if r), 1, 'partial should span several saved ranges')
        self.start()
        self.orbit.call(f'/downloads/{job["id"]}/resume', {})
        self.assert_complete()


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


Fixture.reset()
SERVER = Server(('127.0.0.1', 0), Fixture)
SERVER_URL = f'http://127.0.0.1:{SERVER.server_address[1]}'
threading.Thread(target=SERVER.serve_forever, daemon=True).start()

if __name__ == '__main__':
    try:
        unittest.main(verbosity=2)
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
