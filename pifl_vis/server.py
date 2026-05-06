from __future__ import annotations

import argparse
import json
import socket
import struct
import threading
import time
from dataclasses import dataclass, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

TEMPERATURE_PACKET = 0x01
MODEL_PACKET = 0x02
AGGREGATION_PACKET = 0x03
DISCOVERY_PACKET = 0x04
PROTOCOL_VERSION = 0x01
ROLE_FLAG_AGGREGATOR_ELIGIBLE = 0x01
ROLE_FLAG_AUTO_NEIGHBOR = 0x02
HISTORY_LIMIT = 40

HTML = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>PIFL Federated Learning Dashboard</title>
  <style>
    :root {
      color-scheme: dark;
      --bg-a: #07111c;
      --bg-b: #03070c;
      --panel: rgba(11, 24, 40, 0.92);
      --panel-strong: rgba(17, 37, 58, 0.96);
      --line: rgba(147, 184, 211, 0.18);
      --text: #eef6ff;
      --muted: #9bb4c7;
      --accent: #79d4ff;
      --accent-2: #ffc66b;
      --accent-3: #8bf1c9;
      --accent-4: #ff8f8f;
      --accent-5: #a8b8ff;
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
        radial-gradient(circle at top right, rgba(255, 198, 107, 0.12), transparent 34%),
        linear-gradient(180deg, var(--bg-a) 0%, var(--bg-b) 100%);
    }
    .shell {
      max-width: 1380px;
      margin: 0 auto;
      padding: 28px 18px 44px;
    }
    .hero {
      display: grid;
      gap: 18px;
      grid-template-columns: 1.4fr 1fr;
      align-items: end;
      margin-bottom: 18px;
    }
    .title {
      margin: 0;
      font-size: clamp(2.1rem, 4vw, 3.6rem);
      line-height: 0.94;
      letter-spacing: -0.05em;
    }
    .subtitle {
      margin: 12px 0 0;
      color: var(--muted);
      max-width: 68ch;
      line-height: 1.5;
    }
    .panel {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 24px;
      box-shadow: var(--shadow);
      backdrop-filter: blur(18px);
    }
    .stats {
      display: grid;
      gap: 12px;
      grid-template-columns: repeat(auto-fit, minmax(135px, 1fr));
      padding: 18px;
    }
    .stat {
      padding: 14px 16px;
      border-radius: 18px;
      background: rgba(255, 255, 255, 0.03);
      border: 1px solid rgba(255, 255, 255, 0.05);
    }
    .stat-label {
      display: block;
      margin-bottom: 10px;
      color: var(--muted);
      font-size: 0.78rem;
      letter-spacing: 0.08em;
      text-transform: uppercase;
    }
    .stat-value {
      font-size: 1.55rem;
      font-weight: 700;
      letter-spacing: -0.04em;
    }
    .section {
      display: grid;
      gap: 18px;
      grid-template-columns: 1.5fr 1fr;
      align-items: start;
      margin-bottom: 18px;
    }
    .section-title {
      margin: 0 0 16px;
      color: var(--muted);
      font-size: 0.9rem;
      text-transform: uppercase;
      letter-spacing: 0.08em;
    }
    .canvas {
      padding: 18px;
    }
    .node-grid {
      display: grid;
      gap: 14px;
      grid-template-columns: repeat(auto-fit, minmax(225px, 1fr));
      min-height: 220px;
    }
    .node-card {
      position: relative;
      overflow: hidden;
      display: grid;
      gap: 12px;
      align-content: start;
      min-height: 205px;
      padding: 16px;
      border-radius: 22px;
      border: 1px solid rgba(255, 255, 255, 0.08);
      background: var(--panel-strong);
    }
    .node-card::after {
      content: "";
      position: absolute;
      width: 140px;
      height: 140px;
      right: -26px;
      bottom: -46px;
      border-radius: 999px;
      background: rgba(255, 255, 255, 0.07);
      filter: blur(10px);
    }
    .node-top {
      display: flex;
      justify-content: space-between;
      gap: 10px;
      align-items: baseline;
    }
    .node-id {
      font-size: 1.15rem;
      font-weight: 700;
      letter-spacing: -0.03em;
    }
    .node-age {
      color: var(--muted);
      font-size: 0.78rem;
    }
    .temperature {
      font-size: 2.1rem;
      font-weight: 800;
      letter-spacing: -0.06em;
      line-height: 0.95;
    }
    .chips {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
    }
    .chip {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      padding: 6px 10px;
      border-radius: 999px;
      background: rgba(255, 255, 255, 0.06);
      border: 1px solid rgba(255, 255, 255, 0.08);
      font-size: 0.78rem;
    }
    .mono {
      font-family: "SFMono-Regular", "Menlo", monospace;
      font-size: 0.83rem;
    }
    .node-meta {
      display: grid;
      gap: 6px;
      color: var(--muted);
      font-size: 0.88rem;
    }
    .model-card {
      padding: 16px;
      border-radius: 20px;
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid rgba(255, 255, 255, 0.08);
      margin-bottom: 12px;
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
    .charts {
      display: grid;
      gap: 18px;
      grid-template-columns: 1fr 1fr;
    }
    .chart-shell {
      padding: 18px;
    }
    .chart-frame {
      min-height: 260px;
      border-radius: 18px;
      border: 1px solid rgba(255, 255, 255, 0.06);
      background: rgba(255, 255, 255, 0.03);
      padding: 12px;
    }
    .legend {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      margin-top: 10px;
    }
    .legend-item {
      display: inline-flex;
      align-items: center;
      gap: 7px;
      color: var(--muted);
      font-size: 0.8rem;
    }
    .swatch {
      width: 10px;
      height: 10px;
      border-radius: 999px;
      display: inline-block;
    }
    .empty {
      min-height: 190px;
      display: grid;
      place-items: center;
      text-align: center;
      color: var(--muted);
      border: 1px dashed rgba(255, 255, 255, 0.16);
      border-radius: 18px;
      padding: 28px;
    }
    .footer {
      margin-top: 16px;
      display: flex;
      justify-content: space-between;
      gap: 10px;
      flex-wrap: wrap;
      color: var(--muted);
      font-size: 0.84rem;
    }
    svg text {
      fill: var(--muted);
      font-family: "SFMono-Regular", "Menlo", monospace;
      font-size: 12px;
    }
    @media (max-width: 1040px) {
      .hero, .section, .charts {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <div class="shell">
    <section class="hero">
      <div>
        <h1 class="title">Physics-Informed<br>Federated Learning Dashboard</h1>
        <p class="subtitle">Live FL output for ESP8266 nodes: participant state, local losses, federated rounds, and global model convergence from the custom binary protocol.</p>
      </div>
      <div class="panel stats" id="summary"></div>
    </section>

    <section class="section">
      <div class="panel canvas">
        <h2 class="section-title">FL Participants</h2>
        <div id="grid" class="node-grid"></div>
      </div>
      <div class="panel canvas">
        <h2 class="section-title">Global FL State</h2>
        <div id="global"></div>
      </div>
    </section>

    <section class="charts">
      <div class="panel chart-shell">
        <h2 class="section-title">Local Loss History</h2>
        <div id="loss-chart" class="chart-frame"></div>
      </div>
      <div class="panel chart-shell">
        <h2 class="section-title">Global Convergence</h2>
        <div id="convergence-chart" class="chart-frame"></div>
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
    const lossChart = document.getElementById("loss-chart");
    const convergenceChart = document.getElementById("convergence-chart");
    const status = document.getElementById("status");

    const palette = ["#79D4FF", "#FFC66B", "#8BF1C9", "#FF8F8F", "#A8B8FF", "#9BD6C1", "#F5B3D3", "#D5F26F"];

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
        ["Participants", data.participant_count],
        ["Global Round", data.global_round || 0],
        ["Avg Loss", fmt(data.average_local_loss, 4)],
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

      grid.innerHTML = nodes.map((node, index) => `
        <article class="node-card" style="background:
          linear-gradient(180deg, ${heatColor(node.temperature_c, min, max)}, rgba(8, 19, 31, 0.96)),
          rgba(18, 40, 61, 0.97);">
          <div class="node-top">
            <div class="node-id">${node.label}</div>
            <div class="node-age">${ageLabel(node.age_s)}</div>
          </div>
          <div class="temperature">${fmt(node.temperature_c, 2)}<span class="mono"> C</span></div>
          <div class="chips">
            <span class="chip">round ${node.round || 0}</span>
            <span class="chip">loss ${fmt(node.loss, 4)}</span>
            <span class="chip">weight ${fmt(node.aggregation_weight, 3)}</span>
            ${(node.role_flags & 1) ? `<span class="chip"><span class="swatch" style="background:${palette[index % palette.length]}"></span>agg-capable</span>` : ""}
          </div>
          <div class="node-meta mono">
            <div>w1 ${fmt(node.model.w1)}</div>
            <div>w2 ${fmt(node.model.w2)}</div>
            <div>b ${fmt(node.model.b)}</div>
            <div>${node.address || "unknown source"}</div>
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
          <div class="metric-row"><span class="metric-label">Aggregator</span><span class="mono">${data.global_sender}</span></div>
          <div class="metric-row"><span class="metric-label">Global Round</span><span class="mono">${data.global_round}</span></div>
          <div class="metric-row"><span class="metric-label">Global Loss</span><span class="mono">${fmt(data.global_loss, 4)}</span></div>
          <div class="metric-row"><span class="metric-label">w1</span><span class="mono">${fmt(data.global_model.w1)}</span></div>
          <div class="metric-row"><span class="metric-label">w2</span><span class="mono">${fmt(data.global_model.w2)}</span></div>
          <div class="metric-row"><span class="metric-label">b</span><span class="mono">${fmt(data.global_model.b)}</span></div>
          <div class="metric-row"><span class="metric-label">Updated</span><span class="mono">${ageLabel(data.global_age_s)}</span></div>
        </section>
      `;
    }

    function buildChart(seriesList, options = {}) {
      const width = options.width || 560;
      const height = options.height || 230;
      const margin = { top: 16, right: 18, bottom: 28, left: 48 };
      const innerWidth = width - margin.left - margin.right;
      const innerHeight = height - margin.top - margin.bottom;
      const activeSeries = seriesList.filter((series) => series.points && series.points.length);

      if (!activeSeries.length) {
        return `<div class="empty">Waiting for enough FL telemetry to render this chart.</div>`;
      }

      const allPoints = activeSeries.flatMap((series) => series.points);
      const xValues = allPoints.map((point) => point.x);
      const yValues = allPoints.map((point) => point.y);
      const xMin = Math.min(...xValues);
      let xMax = Math.max(...xValues);
      let yMin = Math.min(...yValues);
      let yMax = Math.max(...yValues);

      if (options.floorZero) {
        yMin = Math.min(0, yMin);
      }
      if (xMin === xMax) {
        xMax = xMin + 1;
      }
      if (yMin === yMax) {
        yMin -= 1;
        yMax += 1;
      }

      const yPad = (yMax - yMin) * 0.12;
      yMin -= yPad;
      yMax += yPad;

      const scaleX = (value) => margin.left + ((value - xMin) / (xMax - xMin)) * innerWidth;
      const scaleY = (value) => margin.top + innerHeight - ((value - yMin) / (yMax - yMin)) * innerHeight;

      const gridLines = [];
      for (let i = 0; i < 5; i += 1) {
        const ratio = i / 4;
        const y = margin.top + ratio * innerHeight;
        const value = yMax - ratio * (yMax - yMin);
        gridLines.push(`
          <line x1="${margin.left}" y1="${y}" x2="${width - margin.right}" y2="${y}" stroke="rgba(255,255,255,0.08)" stroke-width="1" />
          <text x="${margin.left - 8}" y="${y + 4}" text-anchor="end">${fmt(value, 3)}</text>
        `);
      }

      const xTicks = [];
      for (let i = 0; i < 5; i += 1) {
        const ratio = i / 4;
        const value = xMin + ratio * (xMax - xMin);
        const x = scaleX(value);
        xTicks.push(`
          <line x1="${x}" y1="${margin.top + innerHeight}" x2="${x}" y2="${margin.top + innerHeight + 6}" stroke="rgba(255,255,255,0.14)" stroke-width="1" />
          <text x="${x}" y="${height - 6}" text-anchor="middle">${fmt(value, 0)}</text>
        `);
      }

      const paths = activeSeries.map((series) => {
        const points = series.points.map((point) => `${scaleX(point.x)},${scaleY(point.y)}`).join(" ");
        return `<polyline fill="none" stroke="${series.color}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" points="${points}" />`;
      }).join("");

      const legend = activeSeries.map((series) => `
        <span class="legend-item">
          <span class="swatch" style="background:${series.color}"></span>
          ${series.label}
        </span>
      `).join("");

      return `
        <svg viewBox="0 0 ${width} ${height}" width="100%" height="230" role="img" aria-label="${options.title || "chart"}">
          ${gridLines.join("")}
          <line x1="${margin.left}" y1="${margin.top + innerHeight}" x2="${width - margin.right}" y2="${margin.top + innerHeight}" stroke="rgba(255,255,255,0.18)" stroke-width="1.2" />
          ${xTicks.join("")}
          ${paths}
        </svg>
        <div class="legend">${legend}</div>
      `;
    }

    function renderLossChart(data) {
      const series = data.nodes
        .filter((node) => node.loss_history.length)
        .map((node, index) => ({
          label: node.label,
          color: palette[index % palette.length],
          points: node.loss_history.map((point) => ({ x: point.round, y: point.loss }))
        }));
      lossChart.innerHTML = buildChart(series, { title: "Local loss history", floorZero: true });
    }

    function renderConvergenceChart(data) {
      if (!data.global_history.length) {
        convergenceChart.innerHTML = `<div class="empty">Waiting for global rounds to render convergence.</div>`;
        return;
      }
      const series = [
        {
          label: "w1",
          color: "#79D4FF",
          points: data.global_history.map((point) => ({ x: point.round, y: point.model.w1 }))
        },
        {
          label: "w2",
          color: "#FFC66B",
          points: data.global_history.map((point) => ({ x: point.round, y: point.model.w2 }))
        },
        {
          label: "b",
          color: "#8BF1C9",
          points: data.global_history.map((point) => ({ x: point.round, y: point.model.b }))
        }
      ];
      convergenceChart.innerHTML = buildChart(series, { title: "Global convergence" });
    }

    async function refresh() {
      try {
        const response = await fetch("/api/state", { cache: "no-store" });
        const data = await response.json();
        renderSummary(data);
        renderNodes(data.nodes);
        renderGlobal(data);
        renderLossChart(data);
        renderConvergenceChart(data);
        status.textContent = `Listening on UDP ${data.udp_port}, dashboard on HTTP ${data.http_port}, participants ${data.participant_count}, global round ${data.global_round || 0}`;
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
    loss: float | None = None
    round: int = 0
    role_flags: int = 0
    last_seen: float = 0.0
    loss_history: list[dict[str, float | int]] = field(default_factory=list)
    model_history: list[dict[str, float | int]] = field(default_factory=list)


def read_float32_le(buffer: bytes, offset: int) -> float:
    return struct.unpack("<f", buffer[offset:offset+4])[0]

def read_uint16_le(buffer: bytes, offset: int) -> int:
    return int.from_bytes(buffer[offset:offset + 2], "little", signed=False)

def append_history(history: list[dict[str, float | int]], point: dict[str, float | int]) -> None:
    point_round = int(point.get("round", 0))
    for index, existing in enumerate(history):
        if int(existing.get("round", -1)) == point_round and point_round != 0:
            history[index] = point
            break
    else:
        history.append(point)
    if len(history) > HISTORY_LIMIT:
        del history[:-HISTORY_LIMIT]

def decode_packet(packet: bytes) -> dict[str, Any] | None:
    if len(packet) < 2:
        return None
    node_id = packet[0]
    packet_type = packet[1]
    if packet_type == TEMPERATURE_PACKET and len(packet) == 6:
        temperature_c = read_float32_le(packet, 2)
        return {"kind": "temperature", "node_id": node_id, "temperature_c": temperature_c}
    if packet_type == MODEL_PACKET and len(packet) == 24:
        model = ModelParameters(
            w1=read_float32_le(packet, 2),
            w2=read_float32_le(packet, 6),
            b=read_float32_le(packet, 10),
        )
        weight = read_float32_le(packet, 14)
        round_num = read_uint16_le(packet, 18)
        loss = read_float32_le(packet, 20)
        return {
            "kind": "model",
            "node_id": node_id,
            "model": model,
            "weight": weight,
            "round": round_num,
            "loss": loss,
        }
    if packet_type == AGGREGATION_PACKET and len(packet) == 20:
        model = ModelParameters(
            w1=read_float32_le(packet, 2),
            w2=read_float32_le(packet, 6),
            b=read_float32_le(packet, 10),
        )
        round_num = read_uint16_le(packet, 14)
        loss = read_float32_le(packet, 16)
        return {
            "kind": "aggregation",
            "node_id": node_id,
            "model": model,
            "round": round_num,
            "loss": loss,
        }
    if packet_type == DISCOVERY_PACKET and len(packet) == 4 and packet[3] == PROTOCOL_VERSION:
        return {
            "kind": "discovery",
            "node_id": node_id,
            "role_flags": packet[2],
        }
    return None


class DashboardState:
    def __init__(self, udp_port: int, http_port: int, layout: dict[int, dict[str, Any]]):
        self.udp_port = udp_port
        self.http_port = http_port
        self.layout = layout
        self.lock = threading.Lock()
        self.nodes: dict[int, NodeTelemetry] = {}
        self.packet_counters = {"temperature": 0, "model": 0, "aggregation": 0, "discovery": 0}
        self.global_model: ModelParameters | None = None
        self.global_sender: int | None = None
        self.global_seen: float = 0.0
        self.global_round: int = 0
        self.global_loss: float | None = None
        self.global_history: list[dict[str, Any]] = []

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

    def update_discovery(self, node_id: int, role_flags: int, address: str) -> None:
        with self.lock:
            node = self.get_or_create_node(node_id, address)
            node.role_flags = role_flags
            node.last_seen = time.time()
            self.packet_counters["discovery"] += 1

    def update_temperature(self, node_id: int, temperature_c: float, address: str) -> None:
        with self.lock:
            node = self.get_or_create_node(node_id, address)
            node.temperature_c = temperature_c
            node.last_seen = time.time()
            self.packet_counters["temperature"] += 1

    def update_model(
        self,
        node_id: int,
        model: ModelParameters,
        weight: float,
        round_num: int,
        loss: float | None,
        address: str,
    ) -> None:
        with self.lock:
            node = self.get_or_create_node(node_id, address)
            node.model = model
            node.aggregation_weight = weight
            node.round = round_num
            node.loss = loss
            node.last_seen = time.time()
            if round_num > 0:
                append_history(
                    node.model_history,
                    {"round": round_num, "w1": model.w1, "w2": model.w2, "b": model.b},
                )
                if loss is not None:
                    append_history(node.loss_history, {"round": round_num, "loss": loss})
            self.packet_counters["model"] += 1

    def update_global(
        self,
        node_id: int,
        model: ModelParameters,
        round_num: int,
        loss: float | None,
        address: str,
    ) -> None:
        with self.lock:
            node = self.get_or_create_node(node_id, address)
            node.last_seen = time.time()
            self.global_model = model
            self.global_sender = node_id
            self.global_seen = time.time()
            self.global_round = round_num
            self.global_loss = loss
            if round_num > 0:
                append_history(
                    self.global_history,
                    {
                        "round": round_num,
                        "loss": loss,
                        "sender": node_id,
                        "model": {"w1": model.w1, "w2": model.w2, "b": model.b},
                    },
                )
            self.packet_counters["aggregation"] += 1

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            now = time.time()
            nodes = []
            losses = []
            participants = 0
            for node in sorted(self.nodes.values(), key=lambda item: item.node_id):
                if node.round > 0:
                    participants += 1
                if node.loss is not None:
                    losses.append(node.loss)
                nodes.append(
                    {
                        "node_id": node.node_id,
                        "label": node.label,
                        "address": node.address,
                        "temperature_c": node.temperature_c,
                        "aggregation_weight": node.aggregation_weight,
                        "age_s": now - node.last_seen if node.last_seen else None,
                        "round": node.round,
                        "loss": node.loss,
                        "role_flags": node.role_flags,
                        "model": {
                            "w1": node.model.w1,
                            "w2": node.model.w2,
                            "b": node.model.b,
                        },
                        "loss_history": list(node.loss_history),
                        "model_history": list(node.model_history),
                    }
                )
            payload: dict[str, Any] = {
                "udp_port": self.udp_port,
                "http_port": self.http_port,
                "node_count": len(nodes),
                "participant_count": participants,
                "average_local_loss": (sum(losses) / len(losses)) if losses else None,
                "nodes": nodes,
                "packet_counters": dict(self.packet_counters),
                "global_sender": self.global_sender,
                "global_age_s": now - self.global_seen if self.global_seen else None,
                "global_round": self.global_round,
                "global_loss": self.global_loss,
                "global_model": None,
                "global_history": list(self.global_history),
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
            if decoded["kind"] == "discovery":
                state.update_discovery(decoded["node_id"], decoded["role_flags"], source)
            elif decoded["kind"] == "temperature":
                state.update_temperature(decoded["node_id"], decoded["temperature_c"], source)
            elif decoded["kind"] == "model":
                state.update_model(
                    decoded["node_id"],
                    decoded["model"],
                    decoded["weight"],
                    decoded["round"],
                    decoded["loss"],
                    source,
                )
            else:
                state.update_global(
                    decoded["node_id"],
                    decoded["model"],
                    decoded["round"],
                    decoded["loss"],
                    source,
                )
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
    parser = argparse.ArgumentParser(description="FL dashboard for PIFL ESP8266 nodes")
    parser.add_argument("--udp-host", default="0.0.0.0")
    parser.add_argument("--udp-port", type=int, default=4210)
    parser.add_argument("--http-host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--layout", type=Path, default=Path("pifl_vis/layout.sample.json"))
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
