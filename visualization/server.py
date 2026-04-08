from __future__ import annotations

import argparse
import json
import socket
import threading
import time
from dataclasses import dataclass, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

TEMPERATURE_SCALE = 100.0
PARAMETER_SCALE = 1024.0
WEIGHT_SCALE = 256.0
TEMPERATURE_PACKET = 0x01
MODEL_PACKET = 0x02
AGGREGATION_PACKET = 0x03

HTML = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>PIFL Visualisation</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #07111c;
      --panel: rgba(10, 24, 39, 0.92);
      --panel-strong: rgba(18, 40, 61, 0.97);
      --line: rgba(146, 184, 211, 0.24);
      --text: #eef6ff;
      --muted: #9bb4c7;
      --accent: #79d4ff;
      --accent-2: #ffc66b;
      --good: #8bf1c9;
      --bad: #ff8f8f;
      --shadow: 0 20px 60px rgba(0, 0, 0, 0.35);
      font-family: "Avenir Next", "Segoe UI", sans-serif;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      color: var(--text);
      background:
        radial-gradient(circle at top left, rgba(121, 212, 255, 0.18), transparent 30%),
        radial-gradient(circle at top right, rgba(255, 198, 107, 0.14), transparent 32%),
        linear-gradient(180deg, #08121d 0%, #03070c 100%);
    }
    .shell {
      max-width: 1320px;
      margin: 0 auto;
      padding: 32px 20px 48px;
    }
    .hero {
      display: grid;
      gap: 18px;
      grid-template-columns: 1.5fr 1fr;
      align-items: end;
      margin-bottom: 22px;
    }
    .title {
      font-size: clamp(2rem, 4vw, 3.4rem);
      line-height: 0.95;
      letter-spacing: -0.05em;
      margin: 0;
    }
    .subtitle {
      margin: 10px 0 0;
      color: var(--muted);
      max-width: 60ch;
      line-height: 1.5;
    }
    .panel {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 22px;
      box-shadow: var(--shadow);
      backdrop-filter: blur(18px);
    }
    .stats {
      display: grid;
      gap: 12px;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      padding: 18px;
    }
    .stat {
      padding: 14px 16px;
      border-radius: 18px;
      background: rgba(255, 255, 255, 0.03);
      border: 1px solid rgba(255, 255, 255, 0.04);
    }
    .stat-label {
      display: block;
      color: var(--muted);
      font-size: 0.8rem;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      margin-bottom: 10px;
    }
    .stat-value {
      font-size: 1.6rem;
      font-weight: 700;
      letter-spacing: -0.04em;
    }
    .section {
      display: grid;
      gap: 18px;
      grid-template-columns: 1.5fr 1fr;
      align-items: start;
    }
    .canvas {
      padding: 18px;
    }
    .section-title {
      margin: 0 0 16px;
      font-size: 0.95rem;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: 0.08em;
    }
    .node-grid {
      display: grid;
      gap: 14px;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      min-height: 220px;
    }
    .node-card {
      position: relative;
      overflow: hidden;
      padding: 16px;
      min-height: 180px;
      border-radius: 20px;
      border: 1px solid rgba(255, 255, 255, 0.08);
      background: var(--panel-strong);
      display: grid;
      gap: 12px;
      align-content: start;
      transform: translateY(0);
      transition: transform 180ms ease, border-color 180ms ease;
    }
    .node-card:hover {
      transform: translateY(-2px);
      border-color: rgba(121, 212, 255, 0.4);
    }
    .node-card::after {
      content: "";
      position: absolute;
      inset: auto -10% -35% auto;
      width: 120px;
      height: 120px;
      border-radius: 999px;
      background: rgba(255, 255, 255, 0.08);
      filter: blur(10px);
    }
    .node-top {
      display: flex;
      justify-content: space-between;
      align-items: baseline;
      gap: 8px;
    }
    .node-id {
      font-size: 1.2rem;
      font-weight: 700;
      letter-spacing: -0.03em;
    }
    .node-age {
      color: var(--muted);
      font-size: 0.8rem;
    }
    .temperature {
      font-size: 2.4rem;
      letter-spacing: -0.06em;
      font-weight: 800;
      line-height: 0.9;
    }
    .node-meta {
      display: grid;
      gap: 6px;
      color: var(--muted);
      font-size: 0.9rem;
    }
    .chips {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
    }
    .chip {
      padding: 6px 10px;
      border-radius: 999px;
      background: rgba(255, 255, 255, 0.06);
      border: 1px solid rgba(255, 255, 255, 0.08);
      font-size: 0.78rem;
      color: var(--text);
    }
    .mono {
      font-family: "SFMono-Regular", "Menlo", monospace;
      font-size: 0.84rem;
    }
    .model-grid {
      display: grid;
      gap: 12px;
    }
    .model-card {
      padding: 16px;
      border-radius: 20px;
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid rgba(255, 255, 255, 0.08);
    }
    .model-card h3 {
      margin: 0 0 12px;
      font-size: 1rem;
    }
    .metric-row {
      display: flex;
      justify-content: space-between;
      gap: 8px;
      padding: 7px 0;
      border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    }
    .metric-row:last-child {
      border-bottom: 0;
    }
    .metric-label {
      color: var(--muted);
    }
    .empty {
      padding: 28px;
      border-radius: 20px;
      border: 1px dashed rgba(255, 255, 255, 0.16);
      color: var(--muted);
      text-align: center;
    }
    .footer {
      margin-top: 18px;
      color: var(--muted);
      font-size: 0.85rem;
      display: flex;
      justify-content: space-between;
      gap: 10px;
      flex-wrap: wrap;
    }
    @media (max-width: 960px) {
      .hero, .section {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <div class="shell">
    <section class="hero">
      <div>
        <h1 class="title">Physics-Informed<br>Federated Thermal Grid</h1>
        <p class="subtitle">Live telemetry view for ESP8266 nodes broadcasting temperature, local model parameters, and global aggregation packets over the custom binary protocol.</p>
      </div>
      <div class="panel stats" id="summary"></div>
    </section>
    <section class="section">
      <div class="panel canvas">
        <h2 class="section-title">Node Field</h2>
        <div id="grid" class="node-grid"></div>
      </div>
      <div class="panel canvas">
        <h2 class="section-title">Global Model</h2>
        <div id="global" class="model-grid"></div>
      </div>
    </section>
    <div class="footer">
      <span id="status">Waiting for packets...</span>
      <span>Refresh interval: 1 s</span>
    </div>
  </div>
  <script>
    const summary = document.getElementById("summary");
    const grid = document.getElementById("grid");
    const globalPane = document.getElementById("global");
    const status = document.getElementById("status");

    function fmt(value, digits = 3) {
      return Number.isFinite(value) ? value.toFixed(digits) : "n/a";
    }

    function ageLabel(age) {
      if (!Number.isFinite(age)) {
        return "never";
      }
      if (age < 1) {
        return "just now";
      }
      return `${age.toFixed(1)} s ago`;
    }

    function heatColor(temp, min, max) {
      if (!Number.isFinite(temp)) {
        return "rgba(255,255,255,0.06)";
      }
      const span = Math.max(max - min, 0.01);
      const ratio = Math.max(0, Math.min(1, (temp - min) / span));
      const hue = 210 - ratio * 170;
      const alpha = 0.18 + ratio * 0.28;
      return `hsla(${hue}, 85%, 60%, ${alpha})`;
    }

    function renderSummary(data) {
      const items = [
        ["Nodes", data.node_count],
        ["Temp Packets", data.packet_counters.temperature],
        ["Model Packets", data.packet_counters.model],
        ["Global Packets", data.packet_counters.aggregation]
      ];
      summary.innerHTML = items.map(([label, value]) => `
        <div class="stat">
          <span class="stat-label">${label}</span>
          <span class="stat-value">${value}</span>
        </div>
      `).join("");
    }

    function renderNodes(nodes) {
      if (!nodes.length) {
        grid.innerHTML = `<div class="empty">No nodes observed yet. Start the ESP8266 firmware and keep this machine on the same network segment.</div>`;
        return;
      }

      const temperatures = nodes.map((node) => node.temperature_c).filter((value) => Number.isFinite(value));
      const min = temperatures.length ? Math.min(...temperatures) : 0;
      const max = temperatures.length ? Math.max(...temperatures) : 1;

      grid.innerHTML = nodes.map((node) => `
        <article class="node-card" style="background:
          linear-gradient(180deg, ${heatColor(node.temperature_c, min, max)}, rgba(8, 19, 31, 0.96)),
          rgba(18, 40, 61, 0.97);">
          <div class="node-top">
            <div class="node-id">${node.label}</div>
            <div class="node-age">${ageLabel(node.age_s)}</div>
          </div>
          <div class="temperature">${fmt(node.temperature_c, 2)}<span class="mono"> C</span></div>
          <div class="chips">
            <span class="chip">weight ${fmt(node.aggregation_weight, 3)}</span>
            <span class="chip">${node.address || "unknown source"}</span>
          </div>
          <div class="node-meta mono">
            <div>w1 ${fmt(node.model.w1)}</div>
            <div>w2 ${fmt(node.model.w2)}</div>
            <div>b ${fmt(node.model.b)}</div>
          </div>
        </article>
      `).join("");
    }

    function renderGlobal(data) {
      if (!data.global_model) {
        globalPane.innerHTML = `<div class="empty">No global aggregation packet received yet.</div>`;
        return;
      }

      globalPane.innerHTML = `
        <section class="model-card">
          <h3>Latest Aggregate</h3>
          <div class="metric-row"><span class="metric-label">Sender</span><span class="mono">${data.global_sender}</span></div>
          <div class="metric-row"><span class="metric-label">w1</span><span class="mono">${fmt(data.global_model.w1)}</span></div>
          <div class="metric-row"><span class="metric-label">w2</span><span class="mono">${fmt(data.global_model.w2)}</span></div>
          <div class="metric-row"><span class="metric-label">b</span><span class="mono">${fmt(data.global_model.b)}</span></div>
          <div class="metric-row"><span class="metric-label">Updated</span><span class="mono">${ageLabel(data.global_age_s)}</span></div>
        </section>
      `;
    }

    async function refresh() {
      try {
        const response = await fetch("/api/state", { cache: "no-store" });
        const data = await response.json();
        renderSummary(data);
        renderNodes(data.nodes);
        renderGlobal(data);
        status.textContent = `Listening on UDP ${data.udp_port}, dashboard on HTTP ${data.http_port}`;
      } catch (error) {
        status.textContent = `Dashboard error: ${error.message}`;
      }
    }

    refresh();
    setInterval(refresh, 1000);
  </script>
</body>
</html>
"""


@dataclass
class ModelParameters:
    w1: float = 0.0
    w2: float = 0.0
    b: float = 0.0


@dataclass
class NodeTelemetry:
    node_id: int
    label: str
    address: str
    temperature_c: float | None = None
    model: ModelParameters = field(default_factory=ModelParameters)
    aggregation_weight: float = 0.0
    last_seen: float = 0.0


def read_int16_le(buffer: bytes, offset: int) -> int:
    return int.from_bytes(buffer[offset:offset + 2], "little", signed=True)


def read_uint16_le(buffer: bytes, offset: int) -> int:
    return int.from_bytes(buffer[offset:offset + 2], "little", signed=False)


def decode_packet(packet: bytes) -> dict[str, Any] | None:
    if len(packet) < 2:
        return None
    node_id = packet[0]
    packet_type = packet[1]
    if packet_type == TEMPERATURE_PACKET and len(packet) == 4:
        temperature_c = read_int16_le(packet, 2) / TEMPERATURE_SCALE
        return {"kind": "temperature", "node_id": node_id, "temperature_c": temperature_c}
    if packet_type == MODEL_PACKET and len(packet) == 10:
        model = ModelParameters(
            w1=read_int16_le(packet, 2) / PARAMETER_SCALE,
            w2=read_int16_le(packet, 4) / PARAMETER_SCALE,
            b=read_int16_le(packet, 6) / PARAMETER_SCALE,
        )
        weight = read_uint16_le(packet, 8) / WEIGHT_SCALE
        return {"kind": "model", "node_id": node_id, "model": model, "weight": weight}
    if packet_type == AGGREGATION_PACKET and len(packet) == 8:
        model = ModelParameters(
            w1=read_int16_le(packet, 2) / PARAMETER_SCALE,
            w2=read_int16_le(packet, 4) / PARAMETER_SCALE,
            b=read_int16_le(packet, 6) / PARAMETER_SCALE,
        )
        return {"kind": "aggregation", "node_id": node_id, "model": model}
    return None


class DashboardState:
    def __init__(self, udp_port: int, http_port: int, layout: dict[int, dict[str, Any]]):
        self.udp_port = udp_port
        self.http_port = http_port
        self.layout = layout
        self.lock = threading.Lock()
        self.nodes: dict[int, NodeTelemetry] = {}
        self.packet_counters = {"temperature": 0, "model": 0, "aggregation": 0}
        self.global_model: ModelParameters | None = None
        self.global_sender: int | None = None
        self.global_seen: float = 0.0

    def get_or_create_node(self, node_id: int, address: str) -> NodeTelemetry:
        node = self.nodes.get(node_id)
        if node is None:
            layout_entry = self.layout.get(node_id, {})
            label = str(layout_entry.get("label", f"Node {node_id}"))
            node = NodeTelemetry(node_id=node_id, label=label, address=address)
            self.nodes[node_id] = node
        elif address:
            node.address = address
        return node

    def update_temperature(self, node_id: int, temperature_c: float, address: str) -> None:
        with self.lock:
            node = self.get_or_create_node(node_id, address)
            node.temperature_c = temperature_c
            node.last_seen = time.time()
            self.packet_counters["temperature"] += 1

    def update_model(self, node_id: int, model: ModelParameters, weight: float, address: str) -> None:
        with self.lock:
            node = self.get_or_create_node(node_id, address)
            node.model = model
            node.aggregation_weight = weight
            node.last_seen = time.time()
            self.packet_counters["model"] += 1

    def update_global(self, node_id: int, model: ModelParameters, address: str) -> None:
        with self.lock:
            node = self.get_or_create_node(node_id, address)
            node.last_seen = time.time()
            self.global_model = model
            self.global_sender = node_id
            self.global_seen = time.time()
            self.packet_counters["aggregation"] += 1

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            now = time.time()
            nodes = []
            for node in sorted(self.nodes.values(), key=lambda item: item.node_id):
                nodes.append(
                    {
                        "node_id": node.node_id,
                        "label": node.label,
                        "address": node.address,
                        "temperature_c": node.temperature_c,
                        "aggregation_weight": node.aggregation_weight,
                        "age_s": now - node.last_seen if node.last_seen else None,
                        "model": {
                            "w1": node.model.w1,
                            "w2": node.model.w2,
                            "b": node.model.b,
                        },
                    }
                )
            payload: dict[str, Any] = {
                "udp_port": self.udp_port,
                "http_port": self.http_port,
                "node_count": len(nodes),
                "nodes": nodes,
                "packet_counters": dict(self.packet_counters),
                "global_sender": self.global_sender,
                "global_age_s": now - self.global_seen if self.global_seen else None,
                "global_model": None,
            }
            if self.global_model is not None:
                payload["global_model"] = {
                    "w1": self.global_model.w1,
                    "w2": self.global_model.w2,
                    "b": self.global_model.b,
                }
            return payload


def load_layout(path: Path | None) -> dict[int, dict[str, Any]]:
    if path is None or not path.exists():
        return {}
    raw = json.loads(path.read_text(encoding="utf-8"))
    layout: dict[int, dict[str, Any]] = {}
    for key, value in raw.items():
        try:
            layout[int(key)] = value
        except ValueError:
            continue
    return layout


def run_udp_listener(
    state: DashboardState,
    bind_host: str,
    stop_event: threading.Event | None = None,
    ready_event: threading.Event | None = None,
) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((bind_host, state.udp_port))
    sock.settimeout(0.2)
    if ready_event is not None:
        ready_event.set()
    try:
        while stop_event is None or not stop_event.is_set():
            try:
                packet, address = sock.recvfrom(2048)
            except TimeoutError:
                continue
            decoded = decode_packet(packet)
            if decoded is None:
                continue
            source = f"{address[0]}:{address[1]}"
            if decoded["kind"] == "temperature":
                state.update_temperature(decoded["node_id"], decoded["temperature_c"], source)
            elif decoded["kind"] == "model":
                state.update_model(decoded["node_id"], decoded["model"], decoded["weight"], source)
            else:
                state.update_global(decoded["node_id"], decoded["model"], source)
    finally:
        sock.close()


def create_http_server(state: DashboardState, host: str, port: int) -> ThreadingHTTPServer:
    DashboardHandler.state = state
    return ThreadingHTTPServer((host, port), DashboardHandler)


class DashboardHandler(BaseHTTPRequestHandler):
    state: DashboardState

    def do_GET(self) -> None:
        if self.path == "/" or self.path == "/index.html":
            content = HTML.encode("utf-8")
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
            return
        if self.path == "/api/state":
            body = json.dumps(self.state.snapshot()).encode("utf-8")
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_error(HTTPStatus.NOT_FOUND)

    def log_message(self, format: str, *args: Any) -> None:
        return


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Live visualisation dashboard for PIFL ESP8266 nodes")
    parser.add_argument("--udp-host", default="0.0.0.0")
    parser.add_argument("--udp-port", type=int, default=4210)
    parser.add_argument("--http-host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--layout", type=Path, default=Path("visualization/layout.json"))
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    layout = load_layout(args.layout)
    state = DashboardState(args.udp_port, args.http_port, layout)

    listener = threading.Thread(target=run_udp_listener, args=(state, args.udp_host), daemon=True)
    listener.start()

    server = create_http_server(state, args.http_host, args.http_port)
    print(f"Dashboard listening on http://{args.http_host}:{args.http_port}")
    print(f"UDP listener bound to {args.udp_host}:{args.udp_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
