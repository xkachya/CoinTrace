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

  if (name === 'status') {
    startStatusPoll();
  } else {
    stopStatusPoll();
  }
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
    : (ota.pending ? 'Pending (confirmed)' : 'Closed');
  setText('s-ota',     otaWin);
  setText('s-ota-pre', ota.pre_version || '—');
  colorEl('s-ota', ota.ota_window ? 'var(--orange)' :
                   ota.pending    ? 'var(--green)'  : 'var(--muted)');

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

// ── Init ─────────────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
  document.querySelectorAll('nav button').forEach(btn => {
    btn.addEventListener('click', () => switchTab(btn.dataset.tab));
  });

  document.getElementById('match-form').addEventListener('submit', runMatch);

  document.getElementById('refresh-btn').addEventListener('click', () => {
    setConnBadge('connecting');
    fetchStatus();
  });

  // Start on status tab
  switchTab('status');
});
