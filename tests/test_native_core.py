import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from pifl_vis.protocol import decode_packet, wrap_packet, newer_round
ROOT = Path(__file__).resolve().parents[1]

class NativeCoreTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not compiler: raise unittest.SkipTest('C++ compiler unavailable')
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = str(Path(cls.temp.name) / 'native_core')
        flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror']
        if os.environ.get('PIFL_SANITIZE') == '1': flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
        sources = ['src/node_model.cpp', 'src/node_state.cpp', 'src/discretization.cpp',
                   'src/protocol.cpp', 'src/packet_envelope.cpp', 'tests/native_core.cpp']
        subprocess.run([compiler, *flags, '-Itests/native_stubs', '-Iinclude', *sources,
                        '-o', cls.binary], cwd=ROOT, check=True, capture_output=True, text=True)
        cls.fixtures = json.loads(subprocess.check_output([cls.binary, '--fixtures'], text=True))

    def test_native_regressions(self):
        result = subprocess.run([self.binary], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('native regression checks passed', result.stdout)
        print(result.stdout.strip())

    def test_actual_cpp_packets_decode_in_python(self):
        decoded = {kind: decode_packet(bytes.fromhex(wire), 'test-key') for kind, wire in self.fixtures.items()}
        self.assertTrue(all(decoded.values()))
        self.assertEqual(decoded['temperature']['temperature_c'], 26.25)
        self.assertEqual(decoded['model']['sample_count'], 7)
        self.assertEqual(decoded['model']['model'].w2, -.03125)
        self.assertEqual(decoded['aggregation']['mean_contributor_loss'], .125)
        self.assertEqual(decoded['discovery']['boot_id'], 101)

    def test_actual_cpp_packets_decode_in_javascript(self):
        node = shutil.which('node')
        if not node: self.skipTest('Node.js unavailable')
        script = """
          const {parsePacket} = require('./dashboard/protocol');
          const fixtures = JSON.parse(require('fs').readFileSync(0, 'utf8'));
          const result = Object.fromEntries(Object.entries(fixtures).map(([k,v]) => [k,parsePacket(Buffer.from(v,'hex'),'test-key')]));
          process.stdout.write(JSON.stringify(result));
        """
        result = subprocess.run([node, '-e', script], cwd=ROOT, input=json.dumps(self.fixtures),
                                check=True, capture_output=True, text=True)
        data = json.loads(result.stdout)
        self.assertTrue(all(data.values()))
        self.assertEqual(data['temperature']['temperatureC'], 26.25)
        self.assertEqual(data['model']['sampleCount'], 7)
        self.assertEqual(data['aggregation']['meanContributorLoss'], .125)
        self.assertEqual(data['discovery']['bootId'], 101)

    def test_invalid_wire_rejected_by_both_consumers(self):
        valid = bytes.fromhex(self.fixtures['temperature'])
        invalid = [b'', valid[:-1], valid + b'\x00', valid[:-4] + b'\x00'*4,
                   wrap_packet(struct.pack('<BBBf', 1, 1, 1, 26.25), 'test-key'),
                   wrap_packet(struct.pack('<BBBf', 0, 1, 2, 26.25), 'test-key')]
        for value in [float('nan'), float('inf'), 126., -56.]:
            invalid.append(wrap_packet(struct.pack('<BBBf', 1, 1, 2, value), 'test-key'))
        invalid.append(wrap_packet(struct.pack('<BBBfffHfI',1,3,2,float('nan'),0,0,1,.1,101),'test-key'))
        for wire in invalid: self.assertIsNone(decode_packet(wire, 'test-key'))
        node = shutil.which('node')
        if node:
            script = "const p=require('./dashboard/protocol').parsePacket;const a=JSON.parse(require('fs').readFileSync(0,'utf8'));process.stdout.write(JSON.stringify(a.map(x=>p(Buffer.from(x,'hex'),'test-key'))));"
            result = subprocess.run([node,'-e',script],cwd=ROOT,input=json.dumps([x.hex() for x in invalid]),check=True,capture_output=True,text=True)
            self.assertEqual(json.loads(result.stdout), [None]*len(invalid))

    def test_round_serial_arithmetic(self):
        self.assertTrue(newer_round(0, 65535))
        self.assertFalse(newer_round(65535, 0))
        self.assertFalse(newer_round(3, 3))

if __name__ == '__main__': unittest.main()
