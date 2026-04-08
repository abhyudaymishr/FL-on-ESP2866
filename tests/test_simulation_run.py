import json
import socket
import threading
import time
import unittest
import urllib.request

from visualization import server


def find_free_port() -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = int(sock.getsockname()[1])
    sock.close()
    return port


def encode_signed(value: float, scale: float) -> bytes:
    scaled = round(value * scale)
    bounded = max(-32768, min(32767, scaled))
    return int(bounded).to_bytes(2, "little", signed=True)


def encode_unsigned(value: float, scale: float) -> bytes:
    scaled = round(value * scale)
    bounded = max(0, min(65535, scaled))
    return int(bounded).to_bytes(2, "little", signed=False)


def temperature_packet(node_id: int, temperature_c: float) -> bytes:
    return bytes([node_id, server.TEMPERATURE_PACKET]) + encode_signed(
        temperature_c, server.TEMPERATURE_SCALE
    )


def model_packet(node_id: int, w1: float, w2: float, b: float, weight: float) -> bytes:
    return (
        bytes([node_id, server.MODEL_PACKET])
        + encode_signed(w1, server.PARAMETER_SCALE)
        + encode_signed(w2, server.PARAMETER_SCALE)
        + encode_signed(b, server.PARAMETER_SCALE)
        + encode_unsigned(weight, server.WEIGHT_SCALE)
    )


def aggregation_packet(node_id: int, w1: float, w2: float, b: float) -> bytes:
    return (
        bytes([node_id, server.AGGREGATION_PACKET])
        + encode_signed(w1, server.PARAMETER_SCALE)
        + encode_signed(w2, server.PARAMETER_SCALE)
        + encode_signed(b, server.PARAMETER_SCALE)
    )


class SimulationRunTest(unittest.TestCase):
    def setUp(self) -> None:
        self.udp_port = find_free_port()
        self.http_port = find_free_port()
        self.state = server.DashboardState(
            self.udp_port,
            self.http_port,
            {
                1: {"label": "Alpha"},
                2: {"label": "Beta"},
            },
        )
        self.stop_event = threading.Event()
        self.ready_event = threading.Event()
        self.udp_thread = threading.Thread(
            target=server.run_udp_listener,
            args=(self.state, "127.0.0.1", self.stop_event, self.ready_event),
            daemon=True,
        )
        self.udp_thread.start()
        self.assertTrue(self.ready_event.wait(timeout=2.0))

        self.http_server = server.create_http_server(self.state, "127.0.0.1", self.http_port)
        self.http_thread = threading.Thread(target=self.http_server.serve_forever, daemon=True)
        self.http_thread.start()

    def tearDown(self) -> None:
        self.http_server.shutdown()
        self.http_server.server_close()
        self.http_thread.join(timeout=2.0)
        self.stop_event.set()
        self.udp_thread.join(timeout=2.0)

    def fetch_state(self) -> dict:
        with urllib.request.urlopen(
            f"http://127.0.0.1:{self.http_port}/api/state", timeout=2.0
        ) as response:
            return json.loads(response.read().decode("utf-8"))

    def wait_for_packets(self, expected_node_count: int) -> dict:
        deadline = time.time() + 3.0
        snapshot = {}
        while time.time() < deadline:
            snapshot = self.fetch_state()
            if (
                snapshot["node_count"] == expected_node_count
                and snapshot["packet_counters"]["temperature"] >= 2
                and snapshot["packet_counters"]["model"] >= 1
                and snapshot["packet_counters"]["aggregation"] >= 1
            ):
                return snapshot
            time.sleep(0.05)
        self.fail(f"Timed out waiting for simulated packets. Last snapshot: {snapshot}")

    def test_simulated_run_updates_dashboard_state(self) -> None:
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sender.sendto(temperature_packet(1, 26.25), ("127.0.0.1", self.udp_port))
            sender.sendto(model_packet(1, 0.125, -0.03125, 0.5, 1.75), ("127.0.0.1", self.udp_port))
            sender.sendto(temperature_packet(2, 29.50), ("127.0.0.1", self.udp_port))
            sender.sendto(aggregation_packet(1, 0.25, 0.0625, 1.0), ("127.0.0.1", self.udp_port))
        finally:
            sender.close()

        snapshot = self.wait_for_packets(expected_node_count=2)
        root = urllib.request.urlopen(
            f"http://127.0.0.1:{self.http_port}/", timeout=2.0
        ).read().decode("utf-8")

        self.assertIn("Physics-Informed", root)
        self.assertEqual(snapshot["node_count"], 2)
        self.assertEqual(snapshot["global_sender"], 1)
        self.assertEqual(snapshot["packet_counters"]["temperature"], 2)
        self.assertEqual(snapshot["packet_counters"]["model"], 1)
        self.assertEqual(snapshot["packet_counters"]["aggregation"], 1)

        nodes = {node["node_id"]: node for node in snapshot["nodes"]}
        self.assertEqual(nodes[1]["label"], "Alpha")
        self.assertEqual(nodes[2]["label"], "Beta")
        self.assertAlmostEqual(nodes[1]["temperature_c"], 26.25, places=2)
        self.assertAlmostEqual(nodes[2]["temperature_c"], 29.50, places=2)
        self.assertAlmostEqual(nodes[1]["aggregation_weight"], 1.75, places=2)
        self.assertAlmostEqual(nodes[1]["model"]["w1"], 0.125, places=3)
        self.assertAlmostEqual(nodes[1]["model"]["w2"], -0.03125, places=3)
        self.assertAlmostEqual(nodes[1]["model"]["b"], 0.5, places=3)
        self.assertAlmostEqual(snapshot["global_model"]["w1"], 0.25, places=3)
        self.assertAlmostEqual(snapshot["global_model"]["w2"], 0.0625, places=3)
        self.assertAlmostEqual(snapshot["global_model"]["b"], 1.0, places=3)


if __name__ == "__main__":
    unittest.main()
