# Physics-Informed Federated Learning on ESP8266

Physics-informed federated thermal learning on ESP8266, with a separate Cloudflare control plane, a responsive monitoring console, and a GitHub Actions release pipeline.

## Control plane and CI/CD

The new console separates domain rules, API access, application state, and rendering. It includes live gateway monitoring, explicitly labeled simulation, node filtering and inspection, temperature/loss history, snapshot export, dark theme, and a read-only telemetry guide.

The cloud design uses **Workers + Static Assets + D1**, with one LAN gateway and up to 12 boards. The boards keep training and aggregating locally. The gateway forwards authenticated HTTPS snapshots every 30 seconds; D1 stores a latest snapshot and a bounded 24-hour history ring.

```sh
npm ci --ignore-scripts
npm run dev
```

Open [http://127.0.0.1:3000](http://127.0.0.1:3000) and select **Explore simulation**. The local preview does not emulate Cloudflare services. Use the deployed Worker URL for authenticated physical gateway data.

- [Architecture, data structures, tradeoffs and free-tier budget](docs/architecture.md)
- [Deployment, GitHub secrets, LAN bridge and rollback](docs/deployment.md)
- [CI/CD workflow](.github/workflows/ci-cd.yml)

Pull requests run native/Python/protocol tests, cloud SQL/API/domain tests, a pinned ESP8266 build, and release packaging. Configure the connected Cloudflare Workers Build with `npm run ci:cloudflare` and `npm run release:cloudflare` to validate and deploy `fl-on-esp2866` from `main`, followed by authenticated smoke checks. GitHub Actions deployment is an optional alternative enabled by `PIFL_DEPLOY_PROVIDER=github-actions`. Firmware binaries are built as artifacts; boards are flashed separately.

## What the MVP does

- Reads a local temperature signal.
- Maintains a 2D finite-difference state using four logical neighbors.
- Predicts the next temperature with:
  - diffusion term `alpha * Laplacian(T)`
  - learnable correction `w1*T + w2*T^2 + b`
- Updates `w1`, `w2`, and `b` from one-step prediction error.
- Broadcasts:
  - local temperature packets for PDE coupling
  - model packets for federated learning
- Aggregates peer models with sample-weighted FedAvg on the designated or elected aggregator node.

## Project layout

- `platformio.ini`: PlatformIO configuration for `nodemcuv2`.
- `include/config.h`: node identity, WiFi, timing, topology, network secret, and hyperparameters.
- `include/protocol.h` + `src/protocol.cpp`: binary wire protocol codec (Protocol v2).
- `include/packet_envelope.h` + `src/packet_envelope.cpp`: framing envelope and FNV-1a checksum validation.
- `include/discretization.h` + `src/discretization.cpp`: Phase 1 finite-difference Laplacian and explicit Euler time stepping with zero-flux masking.
- `include/node_model.h` + `src/node_model.cpp`: Phase 2 quadratic node model, feature scaling, hybrid prediction, and Adam optimizer updates.
- `include/node_state.h` + `src/node_state.cpp`: physics, learning, neighbor cache, leader election, and sample-weighted FedAvg logic.
- `include/sensor.h` + `src/sensor.cpp`: synthetic or analog/OneWire temperature sources.
- `src/main.cpp`: ESP8266 runtime loop.
- `pifl_vis/server.py`: live UDP FL dashboard server with HTTP and WebSocket streaming.
- `dashboard/`: Node.js and browser-based real-time dashboard with charts.
- `experiments/`: thermal benchmarking harness (`thermal_benchmark.cpp` / `run_benchmark.py`).
- `ppt/generator.py`: self-contained PowerPoint deck generator for the project overview.
- `console/`: responsive browser console, data adapter, state store, and presentation components.
- `shared/domain.mjs`: framework-independent schema, freshness, summary, and guide logic.
- `cloud/`: Workers API, D1 repository, and numbered schema migrations.
- `pifl_vis/bridge.py`: bounded UDP-to-HTTPS LAN gateway.
- `scripts/`: dependency-free packaging, deployment, smoke verification, and rollback.
- `.github/workflows/`: CI/CD and manual code rollback.

## Configuration

Edit [`include/config.h`](include/config.h) before flashing:

- Set `kNodeId` to `0` for automatic chip-based IDs or assign unique IDs manually.
- Set `kAggregatorNodeId` to a fixed node ID, or leave it `0` to elect the lowest online node automatically.
- Fill `kNeighborIds` with the logical north/south/east/west node IDs.
- Set `kWifiSsid` and `kWifiPassword`.
- Choose `SensorMode::Synthetic` for dry runs or `SensorMode::AnalogLinear` for a simple analog sensor.

With the default config, the node runs standalone with synthetic temperature input and no physical neighbors.

## Real-time multi-device mode

Multiple ESP8266 boards can now connect in real time on the same WiFi network.

- Each node broadcasts discovery heartbeats, temperature, and model updates over UDP.
- If `kNodeId` is `0`, each board derives its own node ID from the ESP8266 chip ID.
- If `kNeighborIds` are left as `0`, the runtime can auto-fill the four stencil slots from live peers.
- If `kAggregatorNodeId` is `0`, the node with the lowest active ID becomes the aggregation broadcaster.

Recommended setup:

1. Flash the same firmware to multiple ESP8266 boards.
2. Put all boards on the same WiFi SSID and password.
3. Leave `kNodeId = 0` for automatic unique IDs.
4. Keep `kNeighborIds` at `0` for discovery-based operation, or assign explicit north/south/east/west IDs for a fixed topology.
5. Open the serial monitor on each board and verify `node=<id>` and `peers=<count>` increase as boards come online.

## Phase 1 and 2 implementation

Phase 1 now lives in the discretization module:

- finite-difference Laplacian on the four-neighbor stencil
- zero-flux boundary masking when neighbors are absent
- explicit Euler time stepping for the diffusion PDE

Phase 2 now lives in the node-model module:

- standardized feature scaling $((T - 25.0) / 10.0)$ for well-conditioned polynomial dynamics
- quadratic correction term $w_1 \hat{T} + w_2 \hat{T}^2 + b$
- hybrid physics-learning prediction
- Adam optimizer update with gradient clipping

The runtime keeps these terms visible through `lap`, `diff`, `corr`, and `pred` in the serial log.

## Visualisation module

The repository includes a live dashboard that listens to the UDP packets emitted by the nodes and renders the federated learning state in real time.

The dashboard shows:

- live FL participants and their latest temperatures
- per-node `w1`, `w2`, `b` parameters
- local contributor sample counts and mean training loss
- local FL round and active peers
- latest global aggregate and leader identity
- real-time temperature and loss telemetry charts

Start it from the project root:

```bash
python3 -m pifl_vis.server --http-port 8080 --udp-port 4210
```

Then open [http://127.0.0.1:8080](http://127.0.0.1:8080).

Optional layout labels can be supplied by copying `pifl_vis/layout.sample.json` to `pifl_vis/layout.json` and editing the node names.

Alternatively, a Node.js dashboard backend is available under `dashboard/`:

```bash
cd dashboard && npm install && npm start
```

## Benchmarks and simulation tests

### Numerical benchmark
Run the frozen-holdout benchmark evaluating persistence, physics-only, local-only, centralized, and federated performance:

```bash
python3 experiments/run_benchmark.py
```

### Native and integration test suite
Run the full test suite (including 575 native C++ regression checks, cross-language packet validation, and simulation run):

```bash
python3 -m unittest discover tests
```

## PPT module

The repository includes a pure-Python PPT generator that exports a project overview deck as a real `.pptx` file without external heavy dependencies.

Run it from the project root:

```bash
python3 -m ppt.generator --output artifacts/pifl_overview.pptx
```

The generated deck summarizes the mathematical model, firmware architecture, protocol design, tooling, and run commands.

## Packet format (Protocol v2)

All packets are framed with a 3-byte header and a 4-byte FNV-1a integrity checksum envelope:

```text
| NodeID (1B) | Type (1B) | Version (1B) | Payload (variable) | Checksum (4B) |
```

Packet lengths include the 3-byte header and, in the final column, the 4-byte checksum:

| Type | Body fields | Header + body | Full wire |
| --- | --- | --- | --- |
| `0x01` Temperature | `floatLE temperatureC` | 7 B | 11 B |
| `0x02` ModelUpdate | `floatLE w1,w2,b`, `uint32LE sampleCount`, `uint16LE round`, `floatLE meanLocalLoss`, `uint16LE baseRound`, `uint8 baseLeaderId`, `uint32LE baseLeaderBootId,senderBootId` | 36 B | 40 B |
| `0x03` GlobalAggregation | `floatLE w1,w2,b`, `uint16LE round`, `floatLE meanContributorLoss`, `uint32LE leaderBootId` | 25 B | 29 B |
| `0x04` Discovery | `uint8 roleFlags`, `uint32LE bootId` | 8 B | 12 B |

## Main control loop

Each sample step performs:

1. Read current temperature.
2. Compare the reading to the previous one-step prediction.
3. Compute sensitivity gradients and update `w1`, `w2`, `b` using the Adam optimizer.
4. Recompute Laplacian from the latest neighbor cache with zero-flux boundary handling.
5. Predict the next temperature with hybrid diffusion + quadratic correction.
6. Broadcast temperature every sample.
7. Broadcast local model, sample counts, round, and loss periodically.
8. On the elected aggregator node, perform sample-weighted FedAvg aggregation and broadcast global weights periodically.

## Notes

- Firmware supports UDP broadcast over WiFi or configured MQTT mode. The cloud LAN bridge currently receives UDP.
- Missing neighbors fall back to the local temperature, equivalent to a zero-flux Neumann boundary condition.
- The sensor layer supports synthetic simulation as well as analog and DS18B20 1-Wire sensors in [`src/sensor.cpp`](src/sensor.cpp).
