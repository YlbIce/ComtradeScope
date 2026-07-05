const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => Array.from(document.querySelectorAll(selector));

const colors = [
  '#42c27d',
  '#4aa8ff',
  '#f0b84c',
  '#f06c64',
  '#a78bfa',
  '#46c2c7',
  '#f28cc6',
  '#9bd96b'
];

const state = {
  ws: null,
  wsUrl: '',
  backendInfo: null,
  connected: false,
  pending: new Map(),
  activePage: 'page-waveform',
  record: null,
  selectedAnalog: new Set(),
  cursorA: null,
  cursorB: null,
  draggingCursor: null,
  zoom: { start: 0, end: 1 },
  timeModel: null
};

function requestId() {
  return `req-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function showToast(message) {
  const toast = $('#toast');
  toast.textContent = message;
  toast.classList.add('show');
  window.clearTimeout(showToast.timer);
  showToast.timer = window.setTimeout(() => toast.classList.remove('show'), 2400);
}

async function boot() {
  const info = await window.comtradeScope.getBackendInfo();
  state.backendInfo = info;
  state.wsUrl = info.wsUrl;
  bindEvents();
  connectWebSocket();
  window.comtradeScope.onBackendLog((message) => addBackendLog(message.trim()));
  window.comtradeScope.onBackendExit(() => {
    state.connected = false;
    updateBackendUi('后端已退出');
  });
  drawLoop();
}

function connectWebSocket() {
  if (state.ws) {
    state.ws.close();
  }

  const ws = new WebSocket(state.wsUrl);
  state.ws = ws;

  ws.addEventListener('open', () => {
    state.connected = true;
    updateBackendUi('后端已连接');
    sendCommand('backend:info').catch(() => {});
  });

  ws.addEventListener('message', (event) => {
    handleMessage(JSON.parse(event.data));
  });

  ws.addEventListener('close', () => {
    state.connected = false;
    updateBackendUi('后端未连接');
    window.setTimeout(connectWebSocket, 1200);
  });

  ws.addEventListener('error', () => {
    updateBackendUi('后端连接失败');
  });
}

function sendCommand(type, payload = {}) {
  if (!state.ws || state.ws.readyState !== WebSocket.OPEN) {
    showToast('Native C++ 后端未连接');
    return Promise.reject(new Error('backend disconnected'));
  }

  const id = requestId();
  const message = { requestId: id, type, payload };
  return new Promise((resolve, reject) => {
    state.pending.set(id, { resolve, reject });
    state.ws.send(JSON.stringify(message));
    window.setTimeout(() => {
      if (state.pending.has(id)) {
        state.pending.delete(id);
        reject(new Error(`${type} 请求超时`));
      }
    }, 30000);
  });
}

function handleMessage(message) {
  if (message.type === 'backend:hello') {
    addBackendLog(`${message.payload?.name || 'Backend'} ${message.payload?.version || ''}`);
  }

  if (message.requestId && state.pending.has(message.requestId)) {
    const pending = state.pending.get(message.requestId);
    state.pending.delete(message.requestId);
    if (message.type === 'error') {
      pending.reject(new Error(message.payload?.message || '后端错误'));
    } else {
      pending.resolve(message.payload || {});
    }
  }
}

function updateBackendUi(message) {
  $('#backendState').textContent = message;
  $('#connectionPill').textContent = message;
  $('#connectionPill').classList.toggle('offline', !state.connected);
  $('#startBackendButton').disabled = state.connected;
}

function addBackendLog(message) {
  if (message) {
    console.info(message);
  }
}

async function openComtrade() {
  const file = await window.comtradeScope.openComtradeFile();
  if (!file.ok || file.canceled) {
    return;
  }
  await loadComtrade(file.cfgPath, file.datPath);
}

async function loadDemo() {
  await loadComtrade(state.backendInfo.demoCfgPath, state.backendInfo.demoDatPath);
}

async function loadComtrade(cfgPath, datPath) {
  try {
    showToast('正在解析 COMTRADE 文件');
    const payload = await sendCommand('comtrade:load', {
      cfgPath,
      datPath,
      maxPoints: 6000
    });
    applyRecord(payload);
    showToast('COMTRADE 文件已加载');
  } catch (error) {
    showToast(error.message || 'COMTRADE 加载失败');
  }
}

function applyRecord(payload) {
  state.record = payload;
  state.timeModel = normalizeRecordTimes(payload);
  const analogCount = payload.waveform?.analogChannels?.length || 0;
  state.selectedAnalog = new Set(Array.from({ length: Math.min(analogCount, 6) }, (_, index) => index));
  state.cursorA = 0.25;
  state.cursorB = 0.75;
  state.zoom = { start: 0, end: 1 };
  renderAll();
}

function renderAll() {
  renderMetrics();
  renderChannels();
  renderStats();
  renderConfig();
  renderEvents();
  renderPhasors();
  renderSequence();
  renderReport();
  updateCursorReadout();
}

function renderMetrics() {
  const summary = state.record?.summary || {};
  $('#fileState').textContent = summary.dataFileType || '已加载';
  $('#stationMetric').textContent = summary.stationName || '-';
  $('#analogMetric').textContent = String(summary.analogCount || 0);
  $('#digitalMetric').textContent = String(summary.digitalCount || 0);
  $('#triggerMetric').textContent = summary.triggerTime || '-';
  $('#sampleMetric').textContent = `${summary.sampleCount || 0} samples`;
  $('#durationMetric').textContent = `${formatNumber((summary.duration || 0) * 1000)} ms`;
  $('#rateMetric').textContent = `${formatNumber(summary.sampleRate || 0)} Hz`;
  $('#formatBadge').textContent = `${summary.dataFileType || '-'} / ${summary.revisionYear || '-'}`;
  $('#selectedCount').textContent = `${state.selectedAnalog.size} 通道`;
}

function normalizeRecordTimes(record) {
  const samples = record?.waveform?.samples || [];
  if (samples.length === 0) {
    return {
      rawStart: 0,
      rawEnd: 1,
      rawDuration: 1,
      triggerTime: 0,
      useRelativeAxis: true,
      sampleInterval: 1
    };
  }

  const rawStart = Number(samples[0].time || 0);
  let rawEnd = Number(samples.at(-1)?.time ?? rawStart);
  if (!Number.isFinite(rawEnd) || rawEnd <= rawStart) {
    rawEnd = rawStart + Math.max(1e-6, 1 / Math.max(1, record?.summary?.sampleRate || samples.length));
  }

  const rawDuration = Math.max(1e-9, rawEnd - rawStart);
  const summary = record?.summary || {};
  const parsedStart = parseComtradeDateTime(summary.startTime);
  const parsedTrigger = parseComtradeDateTime(summary.triggerTime);
  let triggerOffset = rawDuration / 2;
  if (parsedStart && parsedTrigger) {
    const offsetSeconds = (parsedTrigger.getTime() - parsedStart.getTime()) / 1000;
    if (Number.isFinite(offsetSeconds) && offsetSeconds >= -rawDuration && offsetSeconds <= rawDuration * 2) {
      triggerOffset = offsetSeconds;
    }
  }

  const sampleInterval = rawDuration / Math.max(1, samples.length - 1);
  return {
    rawStart,
    rawEnd,
    rawDuration,
    triggerTime: rawStart + triggerOffset,
    useRelativeAxis: true,
    sampleInterval
  };
}

function renderChannels() {
  const channels = state.record?.waveform?.analogChannels || [];
  $('#channelList').innerHTML = channels.map((channel, index) => `
    <label class="channel-row">
      <input type="checkbox" data-channel-index="${index}" ${state.selectedAnalog.has(index) ? 'checked' : ''} />
      <span class="swatch" style="background:${colors[index % colors.length]}"></span>
      <strong>${escapeHtml(channel.id || `A${index + 1}`)}</strong>
      <small>${escapeHtml([channel.phase, channel.unit].filter(Boolean).join(' / ') || '-')}</small>
    </label>
  `).join('');

  $$('[data-channel-index]').forEach((input) => {
    input.addEventListener('change', () => {
      const index = Number(input.dataset.channelIndex);
      if (input.checked) {
        state.selectedAnalog.add(index);
      } else {
        state.selectedAnalog.delete(index);
      }
      $('#selectedCount').textContent = `${state.selectedAnalog.size} 通道`;
      renderReport();
    });
  });
}

function renderStats() {
  const rows = state.record?.analogStats || [];
  $('#statsTable').innerHTML = rows.map((item) => `
    <tr>
      <td>${escapeHtml(item.id)}</td>
      <td>${escapeHtml(item.unit || '-')}</td>
      <td>${formatNumber(item.min)}</td>
      <td>${formatNumber(item.max)}</td>
      <td>${formatNumber(item.rms)}</td>
      <td>${formatNumber(item.fundamentalRms)}</td>
      <td>${formatNumber(item.phaseAngle)}°</td>
      <td>${formatPercent(item.thd23)}</td>
    </tr>
  `).join('');
}

function renderConfig() {
  const summary = state.record?.summary || {};
  const warnings = state.record?.warnings || [];
  $('#configInspector').innerHTML = `
    <dt>CFG</dt><dd>${escapeHtml(summary.cfgPath || '-')}</dd>
    <dt>DAT</dt><dd>${escapeHtml(summary.datPath || '-')}</dd>
    <dt>装置</dt><dd>${escapeHtml(summary.deviceId || '-')}</dd>
    <dt>频率</dt><dd>${escapeHtml(summary.frequency || '-')} Hz</dd>
    <dt>启动时间</dt><dd>${escapeHtml(summary.startTime || '-')}</dd>
    <dt>触发时间</dt><dd>${escapeHtml(summary.triggerTime || '-')}</dd>
    <dt>采样率</dt><dd>${formatNumber(summary.sampleRate || 0)} Hz</dd>
    <dt>告警</dt><dd>${warnings.length ? warnings.map(escapeHtml).join('<br>') : '无'}</dd>
  `;
}

function renderEvents() {
  const events = state.record?.digitalEvents || [];
  $('#eventCount').textContent = `${events.length} events`;
  $('#eventList').innerHTML = events.length ? events.slice(0, 600).map((event) => `
    <div class="event-item ${event.edge}">
      <span>${formatAxisTime(event.time)}</span>
      <strong>${escapeHtml(event.channelId)}</strong>
      <small>${event.from} → ${event.to}</small>
    </div>
  `).join('') : '<p class="empty">无数字量变位</p>';
}

function renderPhasors() {
  const phasors = state.record?.phasors || [];
  $('#phasorList').innerHTML = phasors.map((item, index) => `
    <div class="phasor-item">
      <span class="swatch" style="background:${colors[index % colors.length]}"></span>
      <strong>${escapeHtml(item.channelId)}</strong>
      <span>${formatNumber(item.magnitude)} ${escapeHtml(item.unit || '')}</span>
      <span>${formatNumber(item.angle)}°</span>
    </div>
  `).join('');
}

function renderSequence() {
  const sequence = state.record?.sequence || {};
  $('#sequenceBadge').textContent = sequence.available ? `负序率 ${formatPercent(sequence.unbalance)}` : '不可用';
  if (!sequence.available) {
    $('#sequenceInspector').innerHTML = `<dt>状态</dt><dd>${escapeHtml(sequence.reason || '需要三相通道')}</dd>`;
    return;
  }
  $('#sequenceInspector').innerHTML = `
    <dt>正序</dt><dd>${formatComplex(sequence.positive)}</dd>
    <dt>负序</dt><dd>${formatComplex(sequence.negative)}</dd>
    <dt>零序</dt><dd>${formatComplex(sequence.zero)}</dd>
    <dt>负序率</dt><dd>${formatPercent(sequence.unbalance)}</dd>
  `;
}

function renderReport() {
  $('#reportText').value = buildReport();
}

function drawLoop() {
  drawWaveform();
  drawDigitalTimeline();
  drawPhasor();
  requestAnimationFrame(drawLoop);
}

function drawWaveform() {
  const canvas = $('#waveCanvas');
  if (!canvas || canvas.offsetParent === null) return;
  const ctx = setupCanvas(canvas);
  const rect = canvas.getBoundingClientRect();
  const samples = visibleSamples();
  drawPanelGrid(ctx, rect);

  if (!state.record || samples.length === 0) {
    drawEmpty(ctx, rect, '打开 COMTRADE CFG/DAT 后显示故障录波波形');
    return;
  }

  const channels = state.record.waveform.analogChannels || [];
  const selected = Array.from(state.selectedAnalog).filter((index) => channels[index]);
  if (selected.length === 0) {
    drawEmpty(ctx, rect, '请选择至少一个模拟通道');
    return;
  }

  const padding = { left: 52, right: 18, top: 18, bottom: 34 };
  const plot = plotArea(rect, padding);
  const timeRange = visibleTimeRange(samples);
  const laneHeight = plot.height / selected.length;

  selected.forEach((channelIndex, lane) => {
    const values = samples.map((sample) => sample.analog[channelIndex]).filter(Number.isFinite);
    if (values.length === 0) return;
    let min = Math.min(...values);
    let max = Math.max(...values);
    if (min === max) {
      min -= 1;
      max += 1;
    }
    const yTop = plot.y + lane * laneHeight;
    const yMid = yTop + laneHeight / 2;
    const yScale = (laneHeight * 0.38) / Math.max(Math.abs(max), Math.abs(min), 1);

    ctx.strokeStyle = '#26303a';
    ctx.beginPath();
    ctx.moveTo(plot.x, yMid);
    ctx.lineTo(plot.x + plot.width, yMid);
    ctx.stroke();

    ctx.fillStyle = '#a9b6c3';
    ctx.font = '12px Segoe UI';
    ctx.fillText(channels[channelIndex].id || `A${channelIndex + 1}`, 8, yTop + 18);

    ctx.strokeStyle = colors[channelIndex % colors.length];
    ctx.lineWidth = 1.6;
    ctx.beginPath();
    samples.forEach((sample, index) => {
      const x = timeToX(sample.time, plot, timeRange);
      const y = yMid - sample.analog[channelIndex] * yScale;
      if (index === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
  });

  drawCursor(ctx, plot, timeRange, state.cursorA, '#f0b84c', 'A');
  drawCursor(ctx, plot, timeRange, state.cursorB, '#f06c64', 'B');
  drawAxis(ctx, plot, timeRange);
  if ($('#digitalOverlayCheck').checked) {
    drawDigitalOverlay(ctx, plot, timeRange, samples);
  }
}

function drawDigitalTimeline() {
  const canvas = $('#digitalCanvas');
  if (!canvas || canvas.offsetParent === null) return;
  const ctx = setupCanvas(canvas);
  const rect = canvas.getBoundingClientRect();
  const samples = visibleSamples();
  drawPanelGrid(ctx, rect);

  const channels = state.record?.waveform?.digitalChannels || [];
  if (!state.record || samples.length === 0 || channels.length === 0) {
    drawEmpty(ctx, rect, '无数字量通道');
    return;
  }

  const padding = { left: 92, right: 18, top: 18, bottom: 30 };
  const plot = plotArea(rect, padding);
  const timeRange = visibleTimeRange(samples);
  const visibleChannels = channels.slice(0, 16);
  const laneHeight = plot.height / visibleChannels.length;

  visibleChannels.forEach((channel, channelIndex) => {
    const yBase = plot.y + laneHeight * channelIndex + laneHeight * 0.68;
    const high = yBase - laneHeight * 0.45;
    const low = yBase;
    ctx.fillStyle = '#a9b6c3';
    ctx.font = '12px Segoe UI';
    ctx.fillText(channel.id || `D${channelIndex + 1}`, 8, yBase - 4);
    ctx.strokeStyle = colors[channelIndex % colors.length];
    ctx.lineWidth = 1.4;
    ctx.beginPath();
    samples.forEach((sample, index) => {
      const x = timeToX(sample.time, plot, timeRange);
      const y = sample.digital[channelIndex] ? high : low;
      if (index === 0) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    });
    ctx.stroke();
  });
  drawAxis(ctx, plot, timeRange);
}

function drawPhasor() {
  const canvas = $('#phasorCanvas');
  if (!canvas || canvas.offsetParent === null) return;
  const ctx = setupCanvas(canvas);
  const rect = canvas.getBoundingClientRect();
  drawPanelGrid(ctx, rect);

  const phasors = state.record?.phasors || [];
  if (phasors.length === 0) {
    drawEmpty(ctx, rect, '无相量数据');
    return;
  }

  const cx = rect.width / 2;
  const cy = rect.height / 2;
  const radius = Math.min(rect.width, rect.height) * 0.38;
  const maxMagnitude = Math.max(1, ...phasors.map((item) => item.magnitude || 0));

  ctx.strokeStyle = '#34404b';
  ctx.lineWidth = 1;
  for (const r of [0.33, 0.66, 1]) {
    ctx.beginPath();
    ctx.arc(cx, cy, radius * r, 0, Math.PI * 2);
    ctx.stroke();
  }
  ctx.beginPath();
  ctx.moveTo(cx - radius, cy);
  ctx.lineTo(cx + radius, cy);
  ctx.moveTo(cx, cy - radius);
  ctx.lineTo(cx, cy + radius);
  ctx.stroke();

  phasors.forEach((item, index) => {
    const length = (item.magnitude / maxMagnitude) * radius;
    const angle = (item.angle || 0) * Math.PI / 180;
    const x = cx + Math.cos(angle) * length;
    const y = cy + Math.sin(angle) * length;
    ctx.strokeStyle = colors[index % colors.length];
    ctx.fillStyle = colors[index % colors.length];
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(x, y);
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(x, y, 4, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = '#dce6f0';
    ctx.font = '12px Segoe UI';
    ctx.fillText(item.channelId, x + 6, y - 6);
  });
}

function drawDigitalOverlay(ctx, plot, timeRange, samples) {
  const channels = state.record?.waveform?.digitalChannels || [];
  if (channels.length === 0) return;
  ctx.save();
  ctx.globalAlpha = 0.55;
  const yBase = plot.y + plot.height - 10;
  channels.slice(0, 6).forEach((channel, channelIndex) => {
    const y = yBase - channelIndex * 15;
    ctx.strokeStyle = colors[(channelIndex + 3) % colors.length];
    ctx.beginPath();
    samples.forEach((sample, index) => {
      const x = timeToX(sample.time, plot, timeRange);
      const high = y - 8;
      const low = y;
      if (index === 0) ctx.moveTo(x, sample.digital[channelIndex] ? high : low);
      else ctx.lineTo(x, sample.digital[channelIndex] ? high : low);
    });
    ctx.stroke();
  });
  ctx.restore();
}

function drawCursor(ctx, plot, timeRange, cursor, color, label) {
  if (cursor == null) return;
  const time = rangeTime(cursor, timeRange);
  const x = timeToX(time, plot, timeRange);
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.moveTo(x, plot.y);
  ctx.lineTo(x, plot.y + plot.height);
  ctx.stroke();
  ctx.fillStyle = color;
  ctx.font = '12px Segoe UI';
  ctx.fillText(label, x + 4, plot.y + 14);
}

function drawAxis(ctx, plot, timeRange) {
  ctx.fillStyle = '#9aa7b4';
  ctx.font = '12px Segoe UI';
  const tickCount = Math.max(2, Math.min(7, Math.floor(plot.width / 120)));
  for (let i = 0; i <= tickCount; i += 1) {
    const ratio = i / tickCount;
    const t = timeRange.start + (timeRange.end - timeRange.start) * ratio;
    const x = plot.x + plot.width * ratio;
    const label = formatAxisTime(t);
    ctx.fillText(label, x - Math.min(44, label.length * 3.5), plot.y + plot.height + 22);
  }
}

function setupCanvas(canvas) {
  const rect = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.round(rect.width * ratio));
  canvas.height = Math.max(1, Math.round(rect.height * ratio));
  const ctx = canvas.getContext('2d');
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  ctx.clearRect(0, 0, rect.width, rect.height);
  ctx.fillStyle = '#0d1014';
  ctx.fillRect(0, 0, rect.width, rect.height);
  return ctx;
}

function drawPanelGrid(ctx, rect) {
  ctx.strokeStyle = '#222b34';
  ctx.lineWidth = 1;
  for (let x = 0; x < rect.width; x += 48) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, rect.height);
    ctx.stroke();
  }
  for (let y = 0; y < rect.height; y += 36) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(rect.width, y);
    ctx.stroke();
  }
}

function drawEmpty(ctx, rect, text) {
  ctx.fillStyle = '#7d8996';
  ctx.font = '14px Segoe UI';
  ctx.fillText(text, 18, 30);
}

function plotArea(rect, padding) {
  return {
    x: padding.left,
    y: padding.top,
    width: Math.max(1, rect.width - padding.left - padding.right),
    height: Math.max(1, rect.height - padding.top - padding.bottom)
  };
}

function visibleSamples() {
  const samples = state.record?.waveform?.samples || [];
  if (samples.length === 0) return [];
  const normalized = normalizedZoom();
  state.zoom = normalized;
  const startIndex = Math.floor(clamp(Math.floor((samples.length - 1) * normalized.start), 0, samples.length - 1));
  const endIndex = Math.ceil(clamp(Math.ceil((samples.length - 1) * normalized.end), startIndex + 1, samples.length));
  const safeEndIndex = Math.min(samples.length, Math.max(endIndex, startIndex + 2));
  return samples.slice(startIndex, safeEndIndex);
}

function visibleTimeRange(samples) {
  const model = state.timeModel;
  if (!samples.length || !model) return { start: 0, end: 1 };
  const start = Number(samples[0].time || model.rawStart);
  let end = Number(samples.at(-1)?.time ?? start);
  const minSpan = Math.max(1e-9, model.sampleInterval || 1e-6);
  if (!Number.isFinite(end) || end <= start) {
    end = start + minSpan;
  }
  return { start, end: Math.max(end, start + minSpan) };
}

function timeToX(time, plot, range) {
  return plot.x + ((time - range.start) / Math.max(1e-9, range.end - range.start)) * plot.width;
}

function rangeTime(normalized, range) {
  return range.start + (range.end - range.start) * normalized;
}

function canvasNormalizedX(event, canvas) {
  const rect = canvas.getBoundingClientRect();
  const paddingLeft = 52;
  const paddingRight = 18;
  const x = event.clientX - rect.left;
  return clamp((x - paddingLeft) / Math.max(1, rect.width - paddingLeft - paddingRight), 0, 1);
}

function updateCursorFromEvent(event, mode = 'nearest') {
  const normalized = canvasNormalizedX(event, $('#waveCanvas'));
  if (mode === 'nearest') {
    const da = Math.abs((state.cursorA ?? 0) - normalized);
    const db = Math.abs((state.cursorB ?? 1) - normalized);
    state.draggingCursor = da <= db ? 'A' : 'B';
  }
  if (state.draggingCursor === 'A') {
    state.cursorA = normalized;
  } else {
    state.cursorB = normalized;
  }
  updateCursorReadout();
  renderReport();
}

function updateCursorReadout() {
  const samples = visibleSamples();
  const range = visibleTimeRange(samples);
  const timeA = state.cursorA == null ? null : rangeTime(state.cursorA, range);
  const timeB = state.cursorB == null ? null : rangeTime(state.cursorB, range);
  $('#cursorAReadout').textContent = `A: ${timeA == null ? '-' : formatAxisTime(timeA)}`;
  $('#cursorBReadout').textContent = `B: ${timeB == null ? '-' : formatAxisTime(timeB)}`;
  $('#cursorDeltaReadout').textContent = `Δt: ${timeA == null || timeB == null ? '-' : `${formatNumber(Math.abs(timeB - timeA) * 1000)} ms`}`;
  $('#cursorInspector').innerHTML = buildCursorInspector(timeA, timeB);
}

function buildCursorInspector(timeA, timeB) {
  const samples = visibleSamples();
  if (!state.record || !samples.length || timeA == null || timeB == null) {
    return '<dt>状态</dt><dd>等待游标</dd>';
  }
  const nearestA = nearestSample(samples, timeA);
  const nearestB = nearestSample(samples, timeB);
  const channels = state.record.waveform.analogChannels || [];
  const selected = Array.from(state.selectedAnalog).slice(0, 4);
  const rows = [
    `<dt>A 时间</dt><dd>${formatAxisTime(nearestA.time)}</dd>`,
    `<dt>B 时间</dt><dd>${formatAxisTime(nearestB.time)}</dd>`,
    `<dt>Δt</dt><dd>${formatNumber(Math.abs(nearestB.time - nearestA.time) * 1000)} ms</dd>`
  ];
  for (const index of selected) {
    const va = nearestA.analog[index];
    const vb = nearestB.analog[index];
    rows.push(`<dt>${escapeHtml(channels[index]?.id || `A${index + 1}`)}</dt><dd>A ${formatNumber(va)} / B ${formatNumber(vb)} / Δ ${formatNumber(vb - va)}</dd>`);
  }
  return rows.join('');
}

function nearestSample(samples, time) {
  let best = samples[0];
  let bestDelta = Math.abs(best.time - time);
  for (const sample of samples) {
    const delta = Math.abs(sample.time - time);
    if (delta < bestDelta) {
      best = sample;
      bestDelta = delta;
    }
  }
  return best;
}

function selectByUnit(keyword) {
  const channels = state.record?.waveform?.analogChannels || [];
  state.selectedAnalog.clear();
  channels.forEach((channel, index) => {
    const text = `${channel.id || ''} ${channel.unit || ''}`.toLowerCase();
    if (text.includes(keyword)) {
      state.selectedAnalog.add(index);
    }
  });
  renderChannels();
  renderMetrics();
}

function buildReport() {
  if (!state.record) {
    return '未加载 COMTRADE 文件。';
  }
  const summary = state.record.summary || {};
  const lines = [
    '# COMTRADE 故障录波分析摘要',
    '',
    `- 站点：${summary.stationName || '-'}`,
    `- 装置：${summary.deviceId || '-'}`,
    `- 格式：${summary.dataFileType || '-'} / ${summary.revisionYear || '-'}`,
    `- 频率：${summary.frequency || '-'} Hz`,
    `- 启动时间：${summary.startTime || '-'}`,
    `- 触发时间：${summary.triggerTime || '-'}`,
    `- 样本数：${summary.sampleCount || 0}`,
    `- 时长：${formatNumber((summary.duration || 0) * 1000)} ms`,
    `- 采样率：${formatNumber(summary.sampleRate || 0)} Hz`,
    '',
    '## 模拟通道统计',
    '',
    '| 通道 | 单位 | Min | Max | RMS | 基波 RMS | 相角 | 2/3次 THD |',
    '| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |'
  ];
  for (const item of state.record.analogStats || []) {
    lines.push(`| ${item.id} | ${item.unit || '-'} | ${formatNumber(item.min)} | ${formatNumber(item.max)} | ${formatNumber(item.rms)} | ${formatNumber(item.fundamentalRms)} | ${formatNumber(item.phaseAngle)}° | ${formatPercent(item.thd23)} |`);
  }
  lines.push('', '## 数字量变位');
  const events = state.record.digitalEvents || [];
  if (events.length === 0) {
    lines.push('', '无数字量变位。');
  } else {
    lines.push('', '| 时间(ms) | 通道 | 变化 |', '| ---: | --- | --- |');
    for (const event of events.slice(0, 80)) {
      lines.push(`| ${formatNumber(event.time * 1000)} | ${event.channelId} | ${event.from} -> ${event.to} |`);
    }
  }
  const sequence = state.record.sequence || {};
  lines.push('', '## 序分量');
  if (sequence.available) {
    lines.push(`- 正序：${formatComplex(sequence.positive)}`);
    lines.push(`- 负序：${formatComplex(sequence.negative)}`);
    lines.push(`- 零序：${formatComplex(sequence.zero)}`);
    lines.push(`- 负序率：${formatPercent(sequence.unbalance)}`);
  } else {
    lines.push(`- ${sequence.reason || '不可用'}`);
  }
  return `${lines.join('\n')}\n`;
}

async function exportReport() {
  const result = await window.comtradeScope.saveTextFile({
    title: '导出 COMTRADE 分析报告',
    defaultPath: `comtrade-report-${dateStamp()}.md`,
    filters: [{ name: 'Markdown', extensions: ['md'] }],
    content: $('#reportText').value
  });
  if (!result.canceled) {
    showToast(result.ok ? `报告已导出：${result.filePath}` : result.message || '报告导出失败');
  }
}

async function exportEvents() {
  const events = state.record?.digitalEvents || [];
  const content = `time_ms,channel,from,to,edge\n${events.map((event) => [
    formatNumber(event.time * 1000),
    event.channelId,
    event.from,
    event.to,
    event.edge
  ].map(csvCell).join(',')).join('\n')}`;
  const result = await window.comtradeScope.saveTextFile({
    title: '导出数字量事件',
    defaultPath: `comtrade-events-${dateStamp()}.csv`,
    filters: [{ name: 'CSV', extensions: ['csv'] }],
    content
  });
  if (!result.canceled) {
    showToast(result.ok ? `事件已导出：${result.filePath}` : result.message || '事件导出失败');
  }
}

function switchPage(pageId) {
  const page = $(`#${pageId}`);
  if (!page) return;
  state.activePage = pageId;
  $$('.page').forEach((item) => item.classList.toggle('active', item.id === pageId));
  $$('.nav-item').forEach((item) => item.classList.toggle('active', item.dataset.pageTarget === pageId));
  $('#pageTitle').textContent = page.dataset.title || 'ComtradeScope';
  $('#pageEyebrow').textContent = page.dataset.eyebrow || '';
}

function bindEvents() {
  $$('.nav-item').forEach((button) => {
    button.addEventListener('click', () => switchPage(button.dataset.pageTarget));
  });
  $('#startBackendButton').addEventListener('click', async () => {
    const result = await window.comtradeScope.startBackend();
    showToast(result.message);
    window.setTimeout(connectWebSocket, 500);
  });
  $('#openFileButton').addEventListener('click', openComtrade);
  $('#loadDemoButton').addEventListener('click', loadDemo);
  $('#selectVoltageButton').addEventListener('click', () => selectByUnit('v'));
  $('#selectCurrentButton').addEventListener('click', () => selectByUnit('a'));
  $('#selectAllButton').addEventListener('click', () => {
    const channels = state.record?.waveform?.analogChannels || [];
    state.selectedAnalog = new Set(channels.map((_, index) => index));
    renderChannels();
    renderMetrics();
  });
  $('#clearSelectionButton').addEventListener('click', () => {
    state.selectedAnalog.clear();
    renderChannels();
    renderMetrics();
  });
  $('#resetViewButton').addEventListener('click', () => {
    state.zoom = { start: 0, end: 1 };
    state.cursorA = 0.25;
    state.cursorB = 0.75;
    updateCursorReadout();
  });
  $('#fitFaultButton').addEventListener('click', () => {
    state.zoom = { start: 0.38, end: 0.62 };
    updateCursorReadout();
  });
  $('#digitalOverlayCheck').addEventListener('change', () => {});
  $('#exportReportButton').addEventListener('click', exportReport);
  $('#exportEventsButton').addEventListener('click', exportEvents);

  const waveCanvas = $('#waveCanvas');
  waveCanvas.addEventListener('pointerdown', (event) => {
    waveCanvas.setPointerCapture(event.pointerId);
    updateCursorFromEvent(event, 'nearest');
  });
  waveCanvas.addEventListener('pointermove', (event) => {
    if (state.draggingCursor) {
      updateCursorFromEvent(event, 'drag');
    }
  });
  waveCanvas.addEventListener('pointerup', () => {
    state.draggingCursor = null;
  });
  waveCanvas.addEventListener('wheel', (event) => {
    if (!state.record) return;
    event.preventDefault();
    const anchor = canvasNormalizedX(event, waveCanvas);
    const currentZoom = normalizedZoom();
    const currentSpan = currentZoom.end - currentZoom.start;
    const factor = event.deltaY < 0 ? 0.82 : 1.18;
    const nextSpan = clamp(currentSpan * factor, minZoomSpan(), 1);
    const center = currentZoom.start + currentSpan * anchor;
    state.zoom.start = clamp(center - nextSpan * anchor, 0, 1 - nextSpan);
    state.zoom.end = state.zoom.start + nextSpan;
    updateCursorReadout();
  }, { passive: false });
}

function formatComplex(value) {
  if (!value) return '-';
  return `${formatNumber(value.magnitude)} ∠ ${formatNumber(value.angle)}°`;
}

function normalizedZoom() {
  const minSpan = minZoomSpan();
  let start = Number(state.zoom.start);
  let end = Number(state.zoom.end);
  if (!Number.isFinite(start) || !Number.isFinite(end)) {
    start = 0;
    end = 1;
  }
  start = clamp(start, 0, 1);
  end = clamp(end, start + minSpan, 1);
  if (end - start < minSpan) {
    start = clamp(end - minSpan, 0, 1 - minSpan);
    end = start + minSpan;
  }
  return { start, end };
}

function minZoomSpan() {
  const samples = state.record?.waveform?.samples || [];
  if (samples.length < 3) {
    return 1;
  }
  // 极限缩放仍至少保留 3 个采样点，避免时间范围退化成单点。
  return Math.min(1, Math.max(3 / Math.max(1, samples.length - 1), 0.0005));
}

function formatAxisTime(rawSeconds) {
  const model = state.timeModel;
  const value = Number(rawSeconds);
  if (!Number.isFinite(value)) {
    return '-';
  }
  const relativeSeconds = model?.useRelativeAxis ? value - model.triggerTime : value;
  const abs = Math.abs(relativeSeconds);
  const sign = relativeSeconds < -1e-12 ? 'T-' : 'T+';
  if (abs < 1) {
    return `${sign}${formatFixed(abs * 1000, abs < 0.01 ? 3 : 2)} ms`;
  }
  if (abs < 60) {
    return `${sign}${formatFixed(abs, abs < 10 ? 3 : 2)} s`;
  }
  const minutes = Math.floor(abs / 60);
  const seconds = abs - minutes * 60;
  return `${sign}${minutes}:${formatFixed(seconds, 3).padStart(6, '0')}`;
}

function formatFixed(value, digits) {
  return Number(value).toFixed(digits).replace(/\.?0+$/, '');
}

function formatNumber(value) {
  const number = Number(value);
  if (!Number.isFinite(number)) return '-';
  if (Math.abs(number) >= 10000 || (Math.abs(number) > 0 && Math.abs(number) < 0.001)) {
    return number.toExponential(3);
  }
  return number.toFixed(3).replace(/\.?0+$/, '');
}

function formatPercent(value) {
  return `${formatNumber(Number(value || 0) * 100)}%`;
}

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), Math.max(min, max));
}

function csvCell(value) {
  return `"${String(value ?? '').replaceAll('"', '""')}"`;
}

function dateStamp() {
  const now = new Date();
  const pad = (value) => String(value).padStart(2, '0');
  return `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}-${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`;
}

function parseComtradeDateTime(value) {
  const text = String(value || '').trim();
  const match = /^(\d{1,2})\/(\d{1,2})\/(\d{4}),(\d{1,2}):(\d{1,2}):(\d{1,2})(?:\.(\d+))?$/.exec(text);
  if (!match) {
    return null;
  }
  const [, day, month, year, hour, minute, second, fraction = '0'] = match;
  const ms = Number(`0.${fraction}`) * 1000;
  const date = new Date(
    Number(year),
    Number(month) - 1,
    Number(day),
    Number(hour),
    Number(minute),
    Number(second),
    Math.round(ms)
  );
  return Number.isNaN(date.getTime()) ? null : date;
}

function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

boot();
