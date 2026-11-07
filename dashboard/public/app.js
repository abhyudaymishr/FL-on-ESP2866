const socket = io();

// State
const nodes = new Map();
let globalChartTime = 0;

// Chart.js Setup
Chart.defaults.color = '#a0a8b5';
Chart.defaults.font.family = "'Outfit', sans-serif";

const commonChartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  animation: { duration: 0 },
  interaction: { mode: 'index', intersect: false },
  plugins: { legend: { position: 'top', labels: { usePointStyle: true, boxWidth: 6 } } },
  scales: {
    x: { type: 'linear', display: false, min: 0, max: 100 },
    y: { grid: { color: 'rgba(255, 255, 255, 0.05)' } }
  }
};

const tempCtx = document.getElementById('tempChart').getContext('2d');
const tempChart = new Chart(tempCtx, {
  type: 'line',
  data: { datasets: [] },
  options: {
    ...commonChartOptions,
    scales: { ...commonChartOptions.scales, y: { ...commonChartOptions.scales.y, title: { display: true, text: 'Temperature (°C)' } } }
  }
});

const lossCtx = document.getElementById('lossChart').getContext('2d');
const lossChart = new Chart(lossCtx, {
  type: 'line',
  data: { datasets: [] },
  options: {
    ...commonChartOptions,
    scales: { ...commonChartOptions.scales, y: { ...commonChartOptions.scales.y, type: 'logarithmic', title: { display: true, text: 'Loss (Log Scale)' } } }
  }
});

// Helper Functions
const getColorForNode = (nodeId) => {
  const colors = ['#00e5ff', '#00ff88', '#ff3366', '#ffaa00', '#b366ff', '#33b5e5'];
  return colors[nodeId % colors.length];
};

const updateStatusBadge = (connected) => {
  const badge = document.getElementById('connection-status');
  if (connected) {
    badge.textContent = 'Backend Connected';
    badge.classList.remove('disconnected');
  } else {
    badge.textContent = 'Backend Disconnected';
    badge.classList.add('disconnected');
  }
};

const getOrCreateNodeCard = (nodeId) => {
  if (!nodes.has(nodeId)) {
    // Initialize node state
    nodes.set(nodeId, {
      id: nodeId,
      temp: null,
      model: { w1: 0, w2: 0, b: 0 },
      loss: null,
      round: null,
      lastSeen: Date.now(),
      source: 'Unknown'
    });

    // Create DOM element
    const container = document.getElementById('nodes-container');
    const template = document.getElementById('node-card-template');
    const clone = template.content.cloneNode(true);
    
    const card = clone.querySelector('.node-card');
    card.dataset.nodeId = nodeId;
    card.querySelector('.node-id-display').textContent = nodeId.toString(16).toUpperCase();
    card.style.borderTop = `3px solid ${getColorForNode(nodeId)}`;
    
    container.appendChild(clone);
    document.getElementById('active-nodes-badge').textContent = `${nodes.size} Active Nodes`;

    // Add chart datasets
    tempChart.data.datasets.push({
      label: `Node ${nodeId}`,
      borderColor: getColorForNode(nodeId),
      backgroundColor: getColorForNode(nodeId),
      borderWidth: 2,
      pointRadius: 0,
      data: [],
      tension: 0.4
    });
    
    lossChart.data.datasets.push({
      label: `Node ${nodeId}`,
      borderColor: getColorForNode(nodeId),
      backgroundColor: getColorForNode(nodeId),
      borderWidth: 2,
      pointRadius: 0,
      data: [],
      tension: 0.4
    });
  }
  return document.querySelector(`.node-card[data-node-id="${nodeId}"]`);
};

const updateNodeUI = (nodeId) => {
  const node = nodes.get(nodeId);
  const card = document.querySelector(`.node-card[data-node-id="${nodeId}"]`);
  if (!card) return;

  if (node.temp !== null) card.querySelector('.temp-value').textContent = `${node.temp.toFixed(2)} °C`;
  if (node.round !== null) card.querySelector('.round-value').textContent = node.round;
  if (Number.isFinite(node.loss)) card.querySelector('.loss-value').textContent = node.loss.toExponential(2);
  
  card.querySelector('.w1-value').textContent = node.model.w1.toFixed(4);
  card.querySelector('.w2-value').textContent = node.model.w2.toFixed(4);
  card.querySelector('.b-value').textContent = node.model.b.toFixed(4);
  
  card.querySelector('.source-value').textContent = node.source;
  card.querySelector('.last-seen').textContent = `Last seen: Just now`;

  // Highlight animation
  card.classList.remove('highlight-update');
  void card.offsetWidth; // trigger reflow
  card.classList.add('highlight-update');
};

const pushChartData = (nodeId, temp, loss) => {
  const tempDataset = tempChart.data.datasets.find(d => d.label === `Node ${nodeId}`);
  const lossDataset = lossChart.data.datasets.find(d => d.label === `Node ${nodeId}`);
  
  if (temp !== null && tempDataset) {
    tempDataset.data.push({ x: globalChartTime, y: temp });
    if (tempDataset.data.length > 100) tempDataset.data.shift();
  }
  
  if (loss !== null && lossDataset) {
    // Avoid plotting exactly 0 on log scale
    const plotLoss = loss > 0 ? loss : 1e-10;
    lossDataset.data.push({ x: globalChartTime, y: plotLoss });
    if (lossDataset.data.length > 100) lossDataset.data.shift();
  }
};

// Socket Events
socket.on('connect', () => updateStatusBadge(true));
socket.on('disconnect', () => updateStatusBadge(false));

socket.on('packet', (packet) => {
  const { nodeId, type, source } = packet;
  getOrCreateNodeCard(nodeId);
  const node = nodes.get(nodeId);
  
  node.lastSeen = Date.now();
  node.source = source || node.source;

  let updateCharts = false;

  if (type === 'Temperature') {
    node.temp = packet.temperatureC;
    updateCharts = true;
  } else if (type === 'ModelUpdate') {
    node.model = packet.model;
    node.loss = packet.loss;
    node.round = packet.round;
    updateCharts = true;
  } else if (type === 'GlobalAggregation') {
    document.getElementById('aggregate-summary').textContent =
      `Aggregate ${packet.round} · Leader ${nodeId} · Contributor training loss ${packet.meanContributorLoss.toExponential(3)} (not global validation loss)`;
    document.querySelectorAll('.role-badge').forEach(badge => badge.textContent = 'Peer');
    document.querySelector(`.node-card[data-node-id="${nodeId}"] .role-badge`).textContent = 'Aggregator';
  }

  updateNodeUI(nodeId);

  if (updateCharts) {
    pushChartData(nodeId, type === 'Temperature' ? node.temp : null, type === 'ModelUpdate' ? node.loss : null);
  }
});

// Chart global time ticking
setInterval(() => {
  globalChartTime++;
  
  // Advance chart windows
  tempChart.options.scales.x.min = Math.max(0, globalChartTime - 100);
  tempChart.options.scales.x.max = globalChartTime;
  tempChart.update();
  
  lossChart.options.scales.x.min = Math.max(0, globalChartTime - 100);
  lossChart.options.scales.x.max = globalChartTime;
  lossChart.update();

  // Update last seen timers
  const now = Date.now();
  nodes.forEach((node, id) => {
    const card = document.querySelector(`.node-card[data-node-id="${id}"]`);
    if (card) {
      const secondsAgo = Math.floor((now - node.lastSeen) / 1000);
      card.querySelector('.last-seen').textContent = secondsAgo > 6 ? `Offline · ${secondsAgo}s ago` : `Last seen: ${secondsAgo}s ago`;
      card.style.opacity = secondsAgo > 6 ? '0.5' : '1';
    }
  });
  const active = [...nodes.values()].filter(node => now - node.lastSeen <= 6000).length;
  document.getElementById('active-nodes-badge').textContent = `${active} Active Nodes`;
}, 1000);
