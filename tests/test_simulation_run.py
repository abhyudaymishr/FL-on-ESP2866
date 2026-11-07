"""Datagram/state checks always run; the socket integration test needs loopback access."""
import json
import socket
import struct
import threading
import unittest
import urllib.request
from unittest.mock import patch

from pifl_vis import server
from pifl_vis.protocol import wrap_packet

KEY = 'test-key'
def discovery(node=1, boot=101, roles=1):
    return wrap_packet(struct.pack('<BBBBI', node, 4, 2, roles, boot), KEY)
def temperature(node=1, value=26.25):
    return wrap_packet(struct.pack('<BBBf', node, 1, 2, value), KEY)
def model(node=1, round_num=2):
    return wrap_packet(struct.pack('<BBBfffIHfHBII', node, 2, 2, .125, -.03125, .5,
                                   7, round_num, .1875, 0, 0, 0, 101), KEY)
def aggregation(node=1, boot=101, round_num=3):
    return wrap_packet(struct.pack('<BBBfffHfI', node, 3, 2, .25, .0625, 1., round_num, .125, boot), KEY)
def datagrams():
    return [discovery(), temperature(), model(), aggregation()]

class DashboardStateTest(unittest.TestCase):
    def setUp(self):
        self.state = server.DashboardState(4210, 8080, {1: {'label': 'Alpha'}})

    def feed(self, packet):
        return server.process_datagram(self.state, packet, 'test-source', KEY)

    def test_datagrams_update_real_dashboard_state(self):
        for packet in datagrams():
            self.assertTrue(self.feed(packet))
        data = self.state.snapshot()
        self.assertEqual(data['packet_counters'], {'discovery': 1, 'temperature': 1, 'model': 1, 'aggregation': 1})
        self.assertEqual(data['participant_count'], 1)
        self.assertEqual(data['active_node_count'], 1)
        self.assertEqual(data['global_round'], 3)
        self.assertEqual(data['mean_contributor_loss'], .125)
        self.assertNotIn('global_loss', data)
        self.assertEqual(data['nodes'][0]['temperature_c'], 26.25)
        self.assertEqual(data['nodes'][0]['sample_count'], 7)
        self.assertEqual(data['nodes'][0]['label'], 'Alpha')
        self.assertEqual(data['global_model']['w1'], .25)
        self.assertEqual(data['nodes'][0]['model']['w1'], .125)
        json.dumps(data, allow_nan=False)

    def test_stale_duplicate_unelected_and_wrong_boot_aggregates(self):
        for packet in datagrams(): self.feed(packet)
        for packet in [aggregation(round_num=2), aggregation(), aggregation(boot=999), aggregation(node=2)]:
            self.assertFalse(self.feed(packet))
        self.assertEqual(self.state.snapshot()['global_round'], 3)

    def test_new_boot_resets_session_and_history(self):
        for packet in datagrams(): self.feed(packet)
        self.assertTrue(self.feed(discovery(boot=102)))
        self.assertTrue(self.feed(aggregation(boot=102, round_num=1)))
        data = self.state.snapshot()
        self.assertEqual(data['global_round'], 1)
        self.assertEqual(len(data['global_history']), 1)
        self.assertIsNone(data['nodes'][0]['temperature_c'])

    def test_liveness_does_not_refresh_temperature_or_training_data(self):
        with patch('pifl_vis.server.time.monotonic', return_value=100):
            for packet in datagrams(): self.feed(packet)
        with patch('pifl_vis.server.time.monotonic', return_value=120):
            self.feed(discovery())
            data = self.state.snapshot()
        self.assertEqual(data['active_node_count'], 1)
        self.assertEqual(data['participant_count'], 0)
        self.assertIsNone(data['average_local_loss'])
        self.assertIsNone(data['nodes'][0]['temperature_c'])

    def test_corruption_never_changes_state(self):
        wire = bytearray(temperature()); wire[3] ^= 1
        self.assertFalse(self.feed(bytes(wire)))
        self.assertEqual(self.state.snapshot()['node_count'], 0)

class SocketIntegrationTest(unittest.TestCase):
    def test_udp_to_http(self):
        # A restricted environment is reported as a skip, not a successful socket test.
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(('127.0.0.1', 0))
                udp_port = probe.getsockname()[1]
            state = server.DashboardState(udp_port, 0, {})
            http = server.create_http_server(state, '127.0.0.1', 0)
            self.addCleanup(http.server_close)
            stop, ready = threading.Event(), threading.Event()
            listener = threading.Thread(target=server.run_udp_listener,
                args=(state, '127.0.0.1', stop, ready, KEY), daemon=True)
            listener.start()
            self.addCleanup(lambda: (stop.set(), listener.join(timeout=2)))
            self.assertTrue(ready.wait(2))
            http_thread = threading.Thread(target=http.serve_forever, daemon=True)
            http_thread.start()
            self.addCleanup(lambda: (http.shutdown(), http_thread.join(timeout=2)))
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                for packet in datagrams(): sender.sendto(packet, ('127.0.0.1', udp_port))
            # Wait on an event-sized polling interval, with a bounded overall deadline.
            import time
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                with urllib.request.urlopen(f'http://127.0.0.1:{http.server_port}/api/state', timeout=2) as response:
                    data = json.load(response)
                if data['packet_counters']['aggregation'] == 1: break
                stop.wait(.02)
            self.assertEqual(data['global_round'], 3)
            self.assertEqual(data['nodes'][0]['temperature_c'], 26.25)
            with urllib.request.urlopen(f'http://127.0.0.1:{http.server_port}/', timeout=2) as response:
                self.assertIn('Contributor Training Loss', response.read().decode())
        except (PermissionError, OSError) as exc:
            self.skipTest(f'Loopback socket access unavailable: {exc}')

if __name__ == '__main__': unittest.main()
