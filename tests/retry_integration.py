"""Recovery settings and durable retries against local HTTP fixtures only."""
import json
import time
import unittest
from unittest.mock import patch
from range_integration import RangeTests, RangeFixture, DATA
from backend_integration import FILENAME

class RetryTests(RangeTests):
    def policy(self, **patch):
        code, value = self.request('/settings/downloads', patch)
        self.assertEqual(code, 200, value)
        return value

    def test_settings_migrate_validate_and_persist_atomically(self):
        self.start()
        expected = {'downloadRetries':3, 'downloadRetryDelay':10, 'downloadStallTimeout':60, 'downloadMinSpeed':1}
        system = self.request('/system')[1]
        self.assertEqual({k:system[k] for k in expected}, expected)
        bounds = {'downloadRetries':(0,10), 'downloadRetryDelay':(1,300), 'downloadStallTimeout':(15,600), 'downloadMinSpeed':(1,1024)}
        for key,(lo,hi) in bounds.items():
            for bad in [lo-1,hi+1,1.5,'2',True,None]:
                self.assertEqual(self.request('/settings/downloads', {key:bad})[0],400,(key,bad))
            self.assertEqual(self.policy(**{key:hi})[key],hi)
        self.assertEqual(self.request('/settings/downloads', {'typo':1})[0],400)
        self.assertEqual(self.request('/settings/downloads', {'downloadRetries':0},authenticated=False)[0],401)
        self.stop(); self.start()
        self.assertEqual(self.request('/system')[1]['downloadMinSpeed'],1024)
        (self.state/'state.tmp').symlink_to(self.root/'outside')
        self.assertEqual(self.request('/settings/downloads', {'downloadRetries':0,'concurrentDownloads':8})[0],503)
        value=self.request('/system')[1]
        self.assertEqual(value['downloadRetries'],10)
        self.assertEqual(value['concurrentDownloads'],1)

    def test_one_network_failure_does_not_hold_up_another_download(self):
        path, state = self.seed_jobs(2, 'queued')
        state['concurrentDownloads'] = 1
        state['downloadRetryDelay'] = 60
        state['jobs'][0]['release']['url'] = state['jobs'][0]['release']['url'].replace('/fixture','/unavailable')
        path.write_text(json.dumps(state))
        original = RangeFixture.do_GET
        def response(handler):
            if handler.path == '/unavailable':
                handler.send_response(500)
                handler.send_header('Content-Length','0')
                handler.end_headers()
            else: original(handler)
        with patch.object(RangeFixture, 'do_GET', response):
            self.start()
            deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                jobs=self.request('/downloads')[1]
                if jobs[1]['status']=='complete': break
                self.assertNotEqual(jobs[1]['status'],'error',jobs)
                time.sleep(.03)
            else: self.fail(jobs)
            self.assertEqual(jobs[0]['status'],'retrying')
            self.assertEqual((self.drive/'homebrew/fixture-1.ffpfsc').read_bytes(), DATA)

    def test_zero_retries_stops_after_first_failure(self):
        self.start(); self.policy(downloadRetries=0)
        RangeFixture.mode='server500'; self.create()
        self.wait(lambda j:j['status']=='error')
        self.assertEqual(self.saved()['jobs'][0]['attempts'],0)
        before=len(RangeFixture.bounds); time.sleep(2.2)
        self.assertEqual(len(RangeFixture.bounds),before)

    def test_retry_limit_and_provider_delay_are_honoured(self):
        self.start(); self.policy(downloadRetries=2, downloadRetryDelay=1)
        RangeFixture.mode='server500'; began=time.monotonic(); self.create()
        self.wait(lambda j:j['status']=='error',timeout=10)
        self.assertEqual(self.saved()['jobs'][0]['attempts'],2)
        self.assertGreater(time.monotonic()-began,2.5) # server asks for 2 seconds each time
        self.assertFalse((self.drive/'homebrew'/FILENAME).exists())

    def test_retry_reduces_connections_without_reinterpreting_partial_ranges(self):
        self.connections=8
        self.start(); self.policy(downloadRetryDelay=2)
        RangeFixture.mode='tail-drop'; self.create()
        self.wait(lambda j:j['status']=='retrying')
        saved=self.saved()['jobs'][0]
        self.assertEqual(len(saved['rangeReceived']),8)
        self.assertGreater(sum(saved['rangeReceived']),0)
        while RangeFixture.active: time.sleep(.02)
        RangeFixture.peak=0; RangeFixture.mode='normal'
        self.wait(lambda j:j['status']=='complete',timeout=15)
        self.assertLessEqual(RangeFixture.peak,4)
        self.assertEqual(self.saved()['jobs'][0]['rangeReceived'], [(len(DATA)//8)*(i+1)+(len(DATA)%8)*(i+1)//8-((len(DATA)//8)*i+(len(DATA)%8)*i//8) for i in range(8)])
        self.assertEqual((self.drive/'homebrew'/FILENAME).read_bytes(),DATA)

    def test_pause_cancels_pending_retry(self):
        self.start(); self.policy(downloadRetryDelay=1)
        RangeFixture.mode='server500'; jid=self.create()
        self.wait(lambda j:j['status']=='retrying')
        self.request(f'/downloads/{self.job()["id"]}/pause',{})
        before=len(RangeFixture.bounds);time.sleep(2.5)
        self.assertEqual(self.job()['status'],'paused')
        self.assertEqual(len(RangeFixture.bounds),before)

    def test_turning_retries_off_stops_a_pending_retry(self):
        self.start(); self.policy(downloadRetryDelay=1)
        RangeFixture.mode='server500'; self.create()
        self.wait(lambda j:j['status']=='retrying')
        before=len(RangeFixture.bounds)
        self.policy(downloadRetries=0)
        self.wait(lambda j:j['status']=='error')
        time.sleep(2.2)
        self.assertEqual(len(RangeFixture.bounds),before)

    def test_sustained_slow_download_reconnects_and_finishes_exactly(self):
        self.start(); self.policy(downloadRetryDelay=1,downloadStallTimeout=15,downloadMinSpeed=1024)
        RangeFixture.delay=.12; self.create()
        job=self.wait(lambda j:j['status']=='retrying',timeout=22)
        self.assertIn('minimum speed',job['error'])
        self.assertGreater(job['received'],0)
        RangeFixture.delay=.001
        self.wait(lambda j:j['status']=='complete',timeout=12)
        self.assertEqual((self.drive/'homebrew'/FILENAME).read_bytes(),DATA)

if __name__=='__main__':
    suite=unittest.TestSuite(RetryTests(n) for n in RetryTests.__dict__ if n.startswith('test_'))
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(not result.wasSuccessful())
