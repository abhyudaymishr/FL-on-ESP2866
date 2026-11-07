const express = require('express');
const http = require('http');
const {Server} = require('socket.io');
const dgram = require('dgram');
const mqtt = require('mqtt');
const path = require('path');
const {parsePacket, newerRound} = require('./protocol');
const app = express();
const server = http.createServer(app);
const io = new Server(server);
app.use(express.static(path.join(__dirname, 'public')));
const PORT = Number(process.env.PORT || 3000);
const UDP_PORT = Number(process.env.PIFL_UDP_PORT || 4210);
const MQTT_BROKER = process.env.PIFL_MQTT_BROKER;
const MQTT_TOPIC = process.env.PIFL_MQTT_TOPIC || 'pifl/broadcast';
const AGGREGATOR_ID = Number(process.env.PIFL_AGGREGATOR_ID || 0);
const nodes = new Map();
let aggregate = null;
function receive(wire, source) {
  const packet = parsePacket(wire);
  if (!packet) return;
  packet.source = source;
  let state = nodes.get(packet.nodeId) || {};
  if (packet.type === 'GlobalAggregation') {
    const eligible = [...nodes.values()].filter(n => n.discovery && (n.discovery.roleFlags & 1) &&
      Date.now() - n.lastSeen <= 6000).map(n => n.discovery.nodeId);
    const elected = AGGREGATOR_ID || (eligible.length ? Math.min(...eligible) : null);
    if (packet.nodeId !== elected || !state.discovery || state.discovery.bootId !== packet.leaderBootId) return;
    if (aggregate && aggregate.nodeId === packet.nodeId && aggregate.leaderBootId === packet.leaderBootId &&
        !newerRound(packet.round, aggregate.round)) return;
    aggregate = packet;
  } else {
    if (packet.type === 'Discovery') {
      if (state.discovery && state.discovery.bootId !== packet.bootId) state = {};
      state.discovery = packet;
    } else if (packet.type === 'Temperature') state.temperature = packet;
    else {
      if (state.model && !newerRound(packet.round, state.model.round)) return;
      state.model = packet;
    }
  }
  state.lastSeen = Date.now();
  nodes.set(packet.nodeId, state);
  io.emit('packet', packet);
}
io.on('connection', socket => {
  for (const node of nodes.values()) {
    if (Date.now() - node.lastSeen > 6000) continue;
    for (const packet of [node.discovery, node.temperature, node.model]) if (packet) socket.emit('packet', packet);
  }
  if (aggregate) socket.emit('packet', aggregate);
});
const udpServer = dgram.createSocket('udp4');
udpServer.on('message', (msg, peer) => receive(msg, `UDP ${peer.address}`));
udpServer.on('error', err => { console.error(`UDP listener failed: ${err.message}`); process.exitCode = 1; });
udpServer.bind(UDP_PORT);
if (MQTT_BROKER) {
  const client = mqtt.connect(MQTT_BROKER, {connectTimeout: 3000});
  client.on('connect', () => client.subscribe(MQTT_TOPIC));
  client.on('message', (topic, msg) => receive(msg, `MQTT ${MQTT_BROKER}`));
  client.on('error', err => console.error(`MQTT: ${err.message}`));
}
server.listen(PORT, '127.0.0.1', () => console.log(`Dashboard: http://127.0.0.1:${PORT}`));
