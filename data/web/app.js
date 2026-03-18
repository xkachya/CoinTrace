/* CoinTrace Web UI — A-5a MVP — app.js */
'use strict';

const API = '/api/v1';
let statusPollTimer = null;

// ── Helpers ─────────────────────────────────────────────────────────────────

function confColor(conf) {
  if (conf >= 0.7) return 'var(--green)';
  if (conf >= 0.4) return 'var(--orange)';
  return 'var(--red)';
}

function confPct(conf) {
  return (conf * 100).toFixed(1) + '%';
}

function fmtUptime(secs) {
  const h = Math.floor(secs / 3600);
  const m = Math.floor((secs % 3600) / 60);
  const s = secs % 60;
  if (h > 0) return `${h}h ${m}m`;
  if (m > 0) return `${m}m ${s}s`;
  return `${s}s`;
}

function fmtHeap(bytes) {
  return (bytes / 1024).toFixed(1) + ' KB';
}

function setConnBadge(state) {
  const el = document.getElementById('conn-badge');
  if (state === 'online')       { el.textContent = 'Online';      el.className = 'online';  }
  else if (state === 'offline') { el.textContent = 'Offline';     el.className = 'offline'; }
  else                          { el.textContent = 'Connecting…'; el.className = '';         }
}

async function apiFetch(path, opts) {
  const res = await fetch(API + path, opts);
  if (!res.ok) {
    const body = await res.json().catch(() => ({}));
    const err = new Error(body.error || `HTTP ${res.status}`);
    err.status = res.status;
    err.body = body;
    throw err;
  }
  return res.json();
}

// ── Tabs ─────────────────────────────────────────────────────────────────────

function switchTab(name) {
  document.querySelectorAll('nav button').forEach(btn => {
    btn.classList.toggle('active', btn.dataset.tab === name);
  });
  document.querySelectorAll('.page').forEach(pg => {
    pg.classList.toggle('active', pg.id === 'page-' + name);
  });

  stopStatusPoll();
  stopLogPoll();

  if (name === 'status')   { startStatusPoll(); }
  if (name === 'meas')     { initMeas(); }
  if (name === 'log')      { startLogPoll(); }
  if (name === 'settings') { loadSettings(); }
}

// ── Status tab ───────────────────────────────────────────────────────────────

function startStatusPoll() {
  fetchStatus();
  if (!statusPollTimer) {
    statusPollTimer = setInterval(fetchStatus, 5000);
  }
}

function stopStatusPoll() {
  if (statusPollTimer) {
    clearInterval(statusPollTimer);
    statusPollTimer = null;
  }
}

async function fetchStatus() {
  try {
    const [sys, ota, db, sensor] = await Promise.all([
      apiFetch('/status'),
      apiFetch('/ota/status'),
      apiFetch('/database'),
      apiFetch('/sensor/state'),
    ]);
    setConnBadge('online');
    renderStatus(sys, ota, db, sensor);
  } catch (e) {
    setConnBadge('offline');
  }
}

function renderStatus(sys, ota, db, sensor) {
  // Firmware & device
  setText('s-version', sys.version);
  setText('s-uptime',  fmtUptime(sys.uptime));
  setText('s-wifi',    sys.wifi.toUpperCase());
  setText('s-ip',      sys.ip);
  setText('s-ble',     sys.ble.toUpperCase());

  // Heap
  const heapEl = document.getElementById('s-heap');
  heapEl.textContent = fmtHeap(sys.heap);
  // Warn if free heap < 20 KB
  heapEl.className = 'val' + (sys.heap < 20480 ? ' warn' : ' good');

  setText('s-heap-min',  fmtHeap(sys.heap_min));
  setText('s-heap-block', fmtHeap(sys.heap_max_block));

  // Database
  setText('s-db-count', db.ready ? db.count : '—');
  setText('s-db-ready', db.ready ? 'Ready' : 'Not loaded');
  colorEl('s-db-ready', db.ready ? 'var(--green)' : 'var(--orange)');

  // Sensor
  setText('s-sensor', sensor.state.replace(/_/g, ' '));

  // OTA
  const otaWin = ota.ota_window
    ? `Open — ${ota.seconds_left}s left`
    : (ota.pending && !ota.confirmed ? '⚠ Unconfirmed — rollback pending'
     : ota.pending &&  ota.confirmed ? 'Confirmed ✓'
     : 'Closed');
  setText('s-ota',     otaWin);
  setText('s-ota-pre', ota.pre_version || '—');
  colorEl('s-ota', ota.ota_window          ? 'var(--orange)' :
                   ota.pending && !ota.confirmed ? 'var(--red)'    :
                   ota.pending &&  ota.confirmed ? 'var(--green)'  : 'var(--muted)');

  // Timestamp
  setText('s-updated', 'Updated ' + new Date().toLocaleTimeString());
}

function setText(id, val) {
  const el = document.getElementById(id);
  if (el) el.textContent = val;
}

function colorEl(id, color) {
  const el = document.getElementById(id);
  if (el) el.style.color = color;
}

// ── Match tab ────────────────────────────────────────────────────────────────

async function runMatch(e) {
  e.preventDefault();

  const btn    = document.getElementById('match-btn');
  const status = document.getElementById('match-status');
  const result = document.getElementById('match-result');

  const vals = {
    dRp1:             parseFloat(document.getElementById('f-dRp1').value),
    k1:               parseFloat(document.getElementById('f-k1').value),
    k2:               parseFloat(document.getElementById('f-k2').value),
    slope_rp_per_mm_lr: parseFloat(document.getElementById('f-slope').value),
    dL1:              parseFloat(document.getElementById('f-dL1').value),
  };

  for (const [k, v] of Object.entries(vals)) {
    if (isNaN(v)) { status.textContent = `Invalid value for ${k}`; return; }
  }

  btn.disabled = true;
  status.textContent = 'Querying…';
  result.style.display = 'none';

  try {
    const data = await apiFetch('/database/match', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        algo_ver:    1,
        protocol_id: 'p1_UNKNOWN_013mm',
        vector:      vals,
      }),
    });
    status.textContent = '';
    renderMatchResult(data);
  } catch (err) {
    status.textContent = err.message || 'Request failed';
    status.className = 'msg-error';
  } finally {
    btn.disabled = false;
  }
}

function renderMatchResult(data) {
  const wrap = document.getElementById('match-result');
  wrap.style.display = 'block';
  wrap.innerHTML = '';

  if (!data.match) {
    wrap.innerHTML = '<div class="no-match">No match found in database</div>';
    return;
  }

  const conf  = data.conf;
  const color = confColor(conf);

  // Primary card
  const card = document.createElement('div');
  card.className = 'card';
  card.innerHTML = `
    <div class="card-title">Best Match</div>
    <div class="result-main">
      <span class="result-id">${esc(data.coin_name || data.match)}</span>
      <span class="result-conf" style="color:${color}">${confPct(conf)}</span>
    </div>
    <div class="conf-bar-wrap">
      <div class="conf-bar" style="width:${(conf*100).toFixed(1)}%;background:${color}"></div>
    </div>
    <div class="result-name" style="font-family:var(--mono);margin-bottom:8px">
      ${esc(data.match)} &nbsp;·&nbsp; ${esc(data.metal_code)}
    </div>
  `;

  if (data.alternatives && data.alternatives.length) {
    const altTitle = document.createElement('div');
    altTitle.className = 'alt-title';
    altTitle.textContent = 'ALTERNATIVES';
    card.appendChild(altTitle);

    const ul = document.createElement('ul');
    ul.className = 'alt-list';
    data.alternatives.forEach(alt => {
      const li = document.createElement('li');
      const ac = confColor(alt.conf);
      li.innerHTML = `
        <span class="alt-id">${esc(alt.coin_name || alt.match)}</span>
        <div class="alt-bar-wrap"><div class="alt-bar" style="width:${(alt.conf*100).toFixed(1)}%;background:${ac}"></div></div>
        <span class="alt-conf" style="color:${ac}">${confPct(alt.conf)}</span>
      `;
      ul.appendChild(li);
    });
    card.appendChild(ul);
  }

  wrap.appendChild(card);
}

function esc(s) {
  if (!s) return '';
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

// ── Measurements tab ──────────────────────────────────────────────────────────

let measCount   = 0;
let measCurrent = -1;

async function initMeas() {
  try {
    const s = await apiFetch('/status');
    measCount = s.meas_count || 0;
  } catch (_) { measCount = 0; }
  if (measCount === 0) {
    document.getElementById('meas-result').innerHTML =
      '<div class="meas-empty">No measurements yet</div>';
    updateMeasNav();
    return;
  }
  await loadMeasurement(measCount - 1);
}

function updateMeasNav() {
  const label = measCount === 0
    ? '— / —'
    : `${measCurrent + 1} / ${measCount}`;
  setText('meas-nav', label);
  const prev = document.getElementById('meas-prev');
  const next = document.getElementById('meas-next');
  if (prev) prev.disabled = (measCount === 0 || measCurrent <= 0);
  if (next) next.disabled = (measCount === 0 || measCurrent >= measCount - 1);
}

async function loadMeasurement(id) {
  document.getElementById('meas-result').innerHTML =
    '<div class="meas-empty">Loading…</div>';
  try {
    const m = await apiFetch(`/measure/${id}`);
    measCurrent = id;
    updateMeasNav();
    renderMeasurement(m);
  } catch (err) {
    document.getElementById('meas-result').innerHTML =
      `<div class="meas-empty msg-error">${esc(err.message)}</div>`;
    updateMeasNav();
  }
}

function renderMeasurement(m) {
  const conf  = m.conf || 0;
  const color = confColor(conf);
  const ts    = m.ts ? new Date(m.ts * 1000).toLocaleString() : '—';

  const rp = Array.isArray(m.rp) ? m.rp : [];
  const l  = Array.isArray(m.l)  ? m.l  : [];
  const posCount = Math.max(rp.length, l.length);

  let rawRows = '';
  for (let i = 0; i < posCount; i++) {
    rawRows += `<tr><td>${i}mm equiv.</td><td>${rp[i] !== undefined ? rp[i].toFixed(2) : '—'} Ω</td><td>${l[i] !== undefined ? l[i].toFixed(3) : '—'} µH</td></tr>`;
  }

  const card = document.createElement('div');
  card.className = 'card';
  card.innerHTML = `
    <div class="card-title">MEASUREMENT #${esc(String(m.id !== undefined ? m.id : ''))}</div>
    <div class="meas-grid">
      <div class="stat-item"><label>TIME</label><div class="val small">${esc(ts)}</div></div>
      <div class="stat-item"><label>METAL</label><div class="val small"><span class="badge-metal">${esc(m.metal_code || 'UNKN')}</span></div></div>
      <div class="stat-item"><label>COIN</label><div class="val small">${esc(m.coin_name || '—')}</div></div>
      <div class="stat-item">
        <label>CONFIDENCE</label>
        <div class="val" style="color:${color}">${confPct(conf)}</div>
      </div>
    </div>
    <div class="conf-bar-wrap"><div class="conf-bar" style="width:${(conf*100).toFixed(1)}%;background:${color}"></div></div>
    ${posCount > 0 ? `
    <div class="alt-title" style="margin-top:12px">RAW SENSOR DATA</div>
    <table class="raw-table">
      <thead><tr><th>Position</th><th>Rp (Ω)</th><th>L (µH)</th></tr></thead>
      <tbody>${rawRows}</tbody>
    </table>` : ''}
  `;

  const wrap = document.getElementById('meas-result');
  wrap.innerHTML = '';
  wrap.appendChild(card);
}

// ── Log tab ───────────────────────────────────────────────────────────────────────────

let logPollTimer  = null;
let logNextMs     = 0;
let logLineCount  = 0;

const LOG_LEVEL_LABELS = ['DEBUG', 'INFO', 'WARNING', 'ERROR', 'FATAL'];

function startLogPoll() {
  fetchLog();
  if (!logPollTimer) logPollTimer = setInterval(fetchLog, 3000);
}

function stopLogPoll() {
  if (logPollTimer) { clearInterval(logPollTimer); logPollTimer = null; }
}

async function fetchLog() {
  const filter = document.getElementById('log-level-select');
  const level  = filter ? filter.value : 'DEBUG';
  try {
    const data = await apiFetch(`/log?n=100&level=${level}&since_ms=${logNextMs}`);
    const entries = data.entries || [];
    if (entries.length > 0) {
      appendLogEntries(entries);
      logNextMs = data.next_ms || logNextMs;
    }
  } catch (_) { /* device offline — next poll will retry */ }
}

function appendLogEntries(entries) {
  const out = document.getElementById('log-output');
  if (!out) return;

  const atBottom = out.scrollHeight - out.clientHeight - out.scrollTop < 30;

  entries.forEach(e => {
    const lvl = e.level || 'INFO';
    const ms  = String(e.ms || 0).padStart(8);
    const line = document.createElement('span');
    line.className = `log-line log-${lvl}`;
    line.textContent = `[${ms}ms] ${lvl.padEnd(7)} ${(e.comp || '').padEnd(8)}: ${e.msg || ''}`;
    out.appendChild(line);
    out.appendChild(document.createTextNode('\n'));
    logLineCount++;
  });

  setText('log-count', `${logLineCount} lines`);
  if (atBottom) out.scrollTop = out.scrollHeight;
}

function clearLog() {
  const out = document.getElementById('log-output');
  if (out) out.textContent = '';
  logLineCount = 0;
  logNextMs    = 0;
  setText('log-count', '0 lines');
}

// ── Settings tab ────────────────────────────────────────────────────────────────

async function loadSettings() {
  const statusEl = document.getElementById('settings-status');
  statusEl.textContent = 'Loading…';
  statusEl.style.color = 'var(--muted)';
  try {
    const s = await apiFetch('/settings');
    document.getElementById('s-devname').value   = s.dev_name    || '';
    document.getElementById('s-lang').value       = s.lang        || 'en';
    document.getElementById('s-displayrot').value = s.display_rot !== undefined ? s.display_rot : 1;
    document.getElementById('s-brightness').value = s.brightness  !== undefined ? s.brightness  : 128;
    document.getElementById('s-loglevel').value   = s.log_level   !== undefined ? s.log_level   : 1;
    setText('s-brightness-val', s.brightness !== undefined ? s.brightness : 128);
    statusEl.textContent = '';
  } catch (err) {
    statusEl.textContent = 'Failed to load: ' + err.message;
    statusEl.style.color = 'var(--red)';
  }
}

async function saveSettings(e) {
  e.preventDefault();
  const btn      = document.getElementById('settings-save');
  const statusEl = document.getElementById('settings-status');
  const banner   = document.getElementById('restart-banner');

  btn.disabled = true;
  statusEl.textContent = 'Saving…';
  statusEl.style.color = 'var(--muted)';

  const payload = {
    dev_name:    document.getElementById('s-devname').value.trim(),
    lang:        document.getElementById('s-lang').value,
    display_rot: parseInt(document.getElementById('s-displayrot').value, 10),
    brightness:  parseInt(document.getElementById('s-brightness').value, 10),
    log_level:   parseInt(document.getElementById('s-loglevel').value, 10),
  };

  if (!payload.dev_name) {
    statusEl.textContent = 'Device name cannot be empty';
    statusEl.style.color = 'var(--red)';
    btn.disabled = false;
    return;
  }

  try {
    const res = await apiFetch('/settings', {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify(payload),
    });
    statusEl.textContent = 'Saved';
    statusEl.style.color = 'var(--green)';
    if (res.needs_restart && banner) banner.style.display = 'block';
    setTimeout(() => {
      if (statusEl.textContent === 'Saved') statusEl.textContent = '';
    }, 3000);
  } catch (err) {
    statusEl.textContent = 'Error: ' + err.message;
    statusEl.style.color = 'var(--red)';
  } finally {
    btn.disabled = false;
  }
}

// ── Init ────────────────────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
  document.querySelectorAll('nav button').forEach(btn => {
    btn.addEventListener('click', () => switchTab(btn.dataset.tab));
  });

  document.getElementById('match-form').addEventListener('submit', runMatch);

  document.getElementById('refresh-btn').addEventListener('click', () => {
    setConnBadge('connecting');
    fetchStatus();
  });

  document.getElementById('meas-prev').addEventListener('click', () => {
    if (measCurrent > 0) loadMeasurement(measCurrent - 1);
  });
  document.getElementById('meas-next').addEventListener('click', () => {
    if (measCurrent < measCount - 1) loadMeasurement(measCurrent + 1);
  });

  document.getElementById('log-clear-btn').addEventListener('click', clearLog);
  document.getElementById('log-level-select').addEventListener('change', () => {
    clearLog();
    fetchLog();
  });

  document.getElementById('settings-form').addEventListener('submit', saveSettings);
  document.getElementById('s-brightness').addEventListener('input', function () {
    setText('s-brightness-val', this.value);
  });

  // Start on status tab
  switchTab('status');
});
