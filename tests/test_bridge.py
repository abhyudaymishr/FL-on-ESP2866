import json
import struct
import unittest
from unittest.mock import patch
from pifl_vis.bridge import Gateway, NoRedirect, send_snapshot
from pifl_vis.protocol import wrap_packet

KEY = 'test-key'
def discovery(node=1, boot=101):
    return wrap_packet(struct.pack('<BBBBI',node,4,2,1,boot),KEY)
def model(boot=101):
    return wrap_packet(struct.pack('<BBBfffIHfHBII',1,2,2,.125,-.03125,.5,7,2,.1875,0,0,0,boot),KEY)

class BridgeTest(unittest.TestCase):
    def setUp(self):
        self.gateway=Gateway('lab-01',key=KEY)

    def test_bound_session_checks_and_freshness(self):
        self.assertFalse(self.gateway.receive(model(),'source'))
        with patch('pifl_vis.server.time.monotonic',return_value=100):
            self.assertTrue(self.gateway.receive(discovery(),'source'))
            self.assertFalse(self.gateway.receive(model(999),'source'))
            self.assertTrue(self.gateway.receive(model(),'source'))
        data=self.gateway.snapshot(1000000,102)
        self.assertEqual(data['nodes'][0]['modelAt'],998000)
        self.assertEqual(data['nodes'][0]['bootId'],101)
        self.assertEqual(data['counters']['rejected'],2)
        self.assertNotIn('address',data['nodes'][0])
        json.dumps(data,allow_nan=False)
        self.assertTrue(self.gateway.receive(discovery(boot=102),'source'))
        self.assertEqual(self.gateway.snapshot()['nodes'][0]['sampleCount'],0)

    def test_gateway_admission_and_corruption_are_bounded(self):
        for node in range(1,13):self.assertTrue(self.gateway.receive(discovery(node),'source'))
        self.assertFalse(self.gateway.receive(discovery(13),'source'))
        self.assertFalse(self.gateway.receive(b'broken','source'))
        self.assertEqual(len(self.gateway.state.nodes),12)

    def test_transport_validates_https_and_never_follows_redirects(self):
        for endpoint in ['http://example.com','https://user:pass@example.com','https://example.com?token=x']:
            with self.assertRaises(ValueError):send_snapshot(endpoint,'token',{})
        self.assertIsNone(NoRedirect().redirect_request(None,None,302,'',{},'https://other'))

    def test_python_cloud_snapshot_matches_javascript_schema(self):
        import subprocess
        self.gateway.receive(discovery(),'source')
        data=self.gateway.snapshot()
        script="import{validateSnapshot}from'./shared/domain.mjs';const s=JSON.parse(await new Response(process.stdin).text());console.log(validateSnapshot(s,s.capturedAt).gatewayId);"
        result=subprocess.run(['node','--input-type=module','-e',script],input=json.dumps(data),text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout.strip(),'lab-01')
