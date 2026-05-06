const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const dgram = require('dgram');
const mqtt = require('mqtt');
const path = require('path');

const app = express();
const server = http.createServer(app);
const io = new Server(server);

app.use(express.static(path.join(__dirname, 'public')));

// Configuration
const PORT = 3000;
const UDP_PORT = 4210;
const MQTT_BROKER = 'mqtt://192.168.1.100:1883'; // Default fallback, but configurable via env if needed
const MQTT_TOPIC = 'pifl/broadcast';
const NETWORK_SECRET = 'pifl_secret_key_123!';

// Simple FNV-1a hash matching the C++ implementation
function computeSignature(buffer, length) {
  let hash = 2166136261 >>> 0;
  for (let i = 0; i < length; i++) {
    hash ^= buffer[i];
    hash = Math.imul(hash, 16777619) >>> 0;
  }
  for (let i = 0; i < NETWORK_SECRET.length; i++) {
    hash ^= NETWORK_SECRET.charCodeAt(i);
    hash = Math.imul(hash, 16777619) >>> 0;
  }
  return hash >>> 0;
}

function verifyAndUnwrap(buffer) {
  if (buffer.length < 4) return null;
  const payloadLength = buffer.length - 4;
  const expectedSig = computeSignature(buffer, payloadLength);
  const actualSig = buffer.readUInt32LE(payloadLength);
  if (expectedSig !== actualSig) {
    return null; // Invalid signature
  }
  return buffer.subarray(0, payloadLength);
}

function parsePacket(buffer) {
  const payload = verifyAndUnwrap(buffer);
  if (!payload || payload.length < 2) return null;

  const nodeId = payload[0];
  const typeId = payload[1];
  
  let packet = { nodeId, typeId, timestamp: Date.now() };

  try {
    switch (typeId) {
      case 1: // Temperature (6 bytes total)
        if (payload.length === 6) {
          packet.type = 'Temperature';
          packet.temperatureC = payload.readFloatLE(2);
        }
        break;
      case 2: // ModelUpdate (24 bytes total)
        if (payload.length === 24) {
          packet.type = 'ModelUpdate';
          packet.model = {
            w1: payload.readFloatLE(2),
            w2: payload.readFloatLE(6),
            b: payload.readFloatLE(10)
          };
          packet.weight = payload.readFloatLE(14);
          packet.round = payload.readUInt16LE(18);
          packet.loss = payload.readFloatLE(20);
        }
        break;
      case 3: // GlobalAggregation (20 bytes total)
        if (payload.length === 20) {
          packet.type = 'GlobalAggregation';
          packet.model = {
            w1: payload.readFloatLE(2),
            w2: payload.readFloatLE(6),
            b: payload.readFloatLE(10)
          };
          packet.round = payload.readUInt16LE(14);
          packet.loss = payload.readFloatLE(16);
        }
        break;
      case 4: // Discovery (4 bytes total)
        if (payload.length === 4) {
          packet.type = 'Discovery';
          packet.roleFlags = payload[2];
          packet.version = payload[3];
        }
        break;
      default:
        packet.type = 'Unknown';
    }
  } catch (err) {
    console.error("Error parsing payload:", err);
    return null;
  }
  
  return packet.type ? packet : null;
}

// Socket.io
io.on('connection', (socket) => {
  console.log('Frontend client connected');
});

// UDP Server
const udpServer = dgram.createSocket('udp4');
udpServer.on('message', (msg, rinfo) => {
  const packet = parsePacket(msg);
  if (packet) {
    packet.source = `UDP ${rinfo.address}`;
    io.emit('packet', packet);
  }
});
udpServer.on('listening', () => {
  console.log(`UDP Server listening on port ${UDP_PORT}`);
});
udpServer.bind(UDP_PORT);

// MQTT Client
try {
  const mqttClient = mqtt.connect(MQTT_BROKER, { connectTimeout: 3000 });
  mqttClient.on('connect', () => {
    console.log(`MQTT connected to ${MQTT_BROKER}`);
    mqttClient.subscribe(MQTT_TOPIC);
  });
  mqttClient.on('message', (topic, message) => {
    const packet = parsePacket(message);
    if (packet) {
      packet.source = `MQTT ${MQTT_BROKER}`;
      io.emit('packet', packet);
    }
  });
  mqttClient.on('error', (err) => {
    console.log(`MQTT error (expected if no broker running): ${err.message}`);
  });
} catch (e) {
  console.log("MQTT Client failed to initialize. Relying on UDP.");
}

server.listen(PORT, () => {
  console.log(`Dashboard backend running on http://localhost:${PORT}`);
});
