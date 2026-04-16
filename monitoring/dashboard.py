#!/usr/bin/env python3
"""
ULMD Live Dashboard
Self-contained web dashboard for the ULMD Market Data Pipeline.

Usage:
    pip install -r requirements.txt
    python dashboard.py

    # With demo data (no ULMD running needed):
    ULMD_DEMO_MODE=1 python dashboard.py

    Open http://localhost:8080
"""

import os
import csv
import time
import json
import random
import math
import statistics
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
CSV_PATH       = os.environ.get('ULMD_CSV_PATH',            '/tmp/ulmd_latency.csv')
WORKER_METRICS = os.environ.get('ULMD_WORKER_METRICS_PATH', '/tmp/worker_risk_metrics.txt')
PARSER_METRICS = os.environ.get('ULMD_PARSER_METRICS_PATH', '/tmp/parser_metrics.txt')
HEALTH_PATH    = os.environ.get('ULMD_HEALTH_PATH',         '/tmp/parser_health.status')
PORT           = int(os.environ.get('ULMD_DASHBOARD_PORT',  '8080'))
CSV_TAIL       = int(os.environ.get('ULMD_CSV_TAIL_ROWS',   '5000'))
DEMO_MODE      = os.environ.get('ULMD_DEMO_MODE', '0') == '1'

# ---------------------------------------------------------------------------
# Demo data generator (used when ULMD is not running)
# ---------------------------------------------------------------------------
_demo_t = 0.0

def _demo_metrics() -> dict:
    global _demo_t
    _demo_t += 0.3
    base = 350 + 80 * math.sin(_demo_t * 0.4)
    jitter = lambda s: max(10, random.gauss(s, s * 0.15))
    msgs = int(random.gauss(820_000, 30_000))
    return {
        "health": 0,
        "health_label": "HEALTHY",
        "error_rate": round(random.uniform(0, 0.4), 3),
        "total_messages": int(820_000 * (_demo_t / 0.3) + random.randint(0, 1000)),
        "dropped": 0,
        "csv_rows": CSV_TAIL,
        "latency": {
            "p50":  round(jitter(base)),
            "p95":  round(jitter(base * 2.8)),
            "p99":  round(jitter(base * 5.2)),
            "p999": round(jitter(base * 9.5)),
            "mean": round(jitter(base * 1.4)),
        },
        "components": {
            "ingress_to_worker": round(jitter(base * 1.6)),
            "parse_to_worker":   round(jitter(base * 0.9)),
        },
        "symbols": {s: round(jitter(base * random.uniform(0.8, 2.0)))
                    for s in ["AAPL", "MSFT", "GOOGL", "TSLA", "AMZN", "META", "NVDA"]},
        "throughput": {
            "worker_msg_s":   msgs,
            "parser_msg_s":   msgs + random.randint(-5000, 5000),
            "worker_bytes_s": msgs * 64,
            "parser_bytes_s": msgs * 64,
        },
        "ring_occupancy_max": random.randint(12, 180),
    }

# ---------------------------------------------------------------------------
# Real data readers
# ---------------------------------------------------------------------------

def _tail(path: str, n: int) -> list[str]:
    try:
        with open(path, 'rb') as f:
            f.seek(0, 2)
            size = f.tell()
            if size == 0:
                return []
            pos, chunk_size = size, 65536
            lines: list[bytes] = []
            while len(lines) < n + 2 and pos > 0:
                pos = max(0, pos - chunk_size)
                f.seek(pos)
                chunk = f.read(min(chunk_size, size - pos))
                lines = chunk.splitlines()
            return [l.decode('utf-8', errors='replace') for l in lines[-n:]]
    except (FileNotFoundError, IOError):
        return []


def _kv(path: str) -> dict[str, str]:
    out: dict[str, str] = {}
    try:
        for line in open(path):
            if '=' in line:
                k, _, v = line.partition('=')
                out[k.strip()] = v.strip()
    except (FileNotFoundError, IOError):
        pass
    return out


def _pct(data: list[float], p: float) -> float:
    if not data:
        return 0.0
    s = sorted(data)
    idx = (p / 100) * (len(s) - 1)
    lo, hi = int(idx), min(int(idx) + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (idx - lo)


def _real_metrics() -> dict:
    # --- CSV ---
    e2e, ingress, parse_ = [], [], []
    syms: dict[str, list[float]] = {}
    dropped = ring_occ = 0
    rows = [l for l in _tail(CSV_PATH, CSV_TAIL) if l and l[0].isdigit()]
    for row in rows:
        p = row.split(',')
        if len(p) < 15:
            continue
        try:
            sym = p[1][:8]
            lat_i = float(p[7])
            lat_p = float(p[8])
            e2e.append(lat_i)
            ingress.append(lat_i)
            parse_.append(lat_p)
            syms.setdefault(sym, []).append(lat_i)
            dropped  = max(dropped,  int(p[13]))
            ring_occ = max(ring_occ, int(p[11]))
        except (ValueError, IndexError):
            pass

    # --- metrics files ---
    wm = _kv(WORKER_METRICS)
    pm = _kv(PARSER_METRICS)
    hs = _kv(HEALTH_PATH)
    status_map = {'HEALTHY': 0, 'DEGRADED': 1, 'UNHEALTHY': 2}
    health_str = hs.get('status', 'UNKNOWN')

    return {
        "health":       status_map.get(health_str, 3),
        "health_label": health_str,
        "error_rate":   float(hs.get('error_rate_percent', 0)),
        "total_messages": int(float(wm.get('total_messages', 0))),
        "dropped":      dropped,
        "csv_rows":     len(rows),
        "latency": {
            "p50":  round(_pct(e2e, 50)),
            "p95":  round(_pct(e2e, 95)),
            "p99":  round(_pct(e2e, 99)),
            "p999": round(_pct(e2e, 99.9)),
            "mean": round(statistics.mean(e2e) if e2e else 0),
        },
        "components": {
            "ingress_to_worker": round(_pct(ingress, 95)),
            "parse_to_worker":   round(_pct(parse_,  95)),
        },
        "symbols": {sym: round(_pct(vals, 95)) for sym, vals in syms.items()},
        "throughput": {
            "worker_msg_s":   int(float(wm.get('messages_per_sec', 0))),
            "parser_msg_s":   int(float(pm.get('messages_per_sec', 0))),
            "worker_bytes_s": int(float(wm.get('bytes_per_sec', 0))),
            "parser_bytes_s": int(float(pm.get('bytes_per_sec', 0))),
        },
        "ring_occupancy_max": ring_occ,
    }


def get_metrics() -> dict:
    return _demo_metrics() if DEMO_MODE else _real_metrics()

# ---------------------------------------------------------------------------
# HTML page
# ---------------------------------------------------------------------------
HTML = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ULMD — Market Data Pipeline Dashboard</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.2/dist/chart.umd.min.js"></script>
<style>
  :root {
    --bg:       #0f1117;
    --surface:  #1a1d27;
    --border:   #2a2d3e;
    --text:     #e2e8f0;
    --muted:    #64748b;
    --green:    #22c55e;
    --yellow:   #eab308;
    --orange:   #f97316;
    --red:      #ef4444;
    --blue:     #3b82f6;
    --purple:   #a855f7;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { background: var(--bg); color: var(--text); font-family: 'Segoe UI', system-ui, sans-serif; }

  header {
    display: flex; align-items: center; justify-content: space-between;
    padding: 16px 28px; border-bottom: 1px solid var(--border);
    background: var(--surface);
  }
  header h1 { font-size: 1.2rem; font-weight: 700; letter-spacing: .5px; }
  header h1 span { color: var(--blue); }
  .badge {
    font-size: .7rem; padding: 3px 10px; border-radius: 999px;
    font-weight: 600; letter-spacing: .5px;
  }
  .badge.live  { background: #22c55e22; color: var(--green); border: 1px solid var(--green); }
  .badge.demo  { background: #a855f722; color: var(--purple); border: 1px solid var(--purple); }
  #last-update { font-size: .75rem; color: var(--muted); }

  main { padding: 20px 28px; }

  /* stat cards row */
  .stats { display: grid; grid-template-columns: repeat(auto-fill, minmax(160px, 1fr)); gap: 14px; margin-bottom: 20px; }
  .stat-card {
    background: var(--surface); border: 1px solid var(--border);
    border-radius: 10px; padding: 14px 16px;
  }
  .stat-card .label { font-size: .7rem; color: var(--muted); text-transform: uppercase; letter-spacing: .8px; margin-bottom: 6px; }
  .stat-card .value { font-size: 1.5rem; font-weight: 700; }
  .stat-card .sub   { font-size: .7rem; color: var(--muted); margin-top: 4px; }

  /* charts grid */
  .charts { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
  .chart-card {
    background: var(--surface); border: 1px solid var(--border);
    border-radius: 10px; padding: 18px;
  }
  .chart-card.full { grid-column: 1 / -1; }
  .chart-card h3 { font-size: .8rem; color: var(--muted); text-transform: uppercase; letter-spacing: .8px; margin-bottom: 14px; }
  .chart-card canvas { max-height: 240px; }

  /* symbol table */
  table { width: 100%; border-collapse: collapse; font-size: .82rem; }
  th { color: var(--muted); font-weight: 600; text-align: left; padding: 6px 10px; border-bottom: 1px solid var(--border); font-size: .7rem; text-transform: uppercase; }
  td { padding: 7px 10px; border-bottom: 1px solid var(--border); }
  tr:last-child td { border-bottom: none; }
  .bar-cell { display: flex; align-items: center; gap: 8px; }
  .bar { height: 6px; border-radius: 3px; min-width: 2px; }

  @media (max-width: 768px) { .charts { grid-template-columns: 1fr; } .chart-card.full { grid-column: 1; } }
</style>
</head>
<body>

<header>
  <h1>ULMD <span>Market Data Pipeline</span></h1>
  <div style="display:flex;align-items:center;gap:12px">
    <span id="mode-badge" class="badge"></span>
    <span id="last-update"></span>
  </div>
</header>

<main>
  <!-- Stat cards -->
  <div class="stats">
    <div class="stat-card">
      <div class="label">Health</div>
      <div class="value" id="s-health">—</div>
    </div>
    <div class="stat-card">
      <div class="label">Messages</div>
      <div class="value" id="s-msgs">—</div>
      <div class="sub">total processed</div>
    </div>
    <div class="stat-card">
      <div class="label">Throughput</div>
      <div class="value" id="s-tput">—</div>
      <div class="sub">msg / sec</div>
    </div>
    <div class="stat-card">
      <div class="label">Error Rate</div>
      <div class="value" id="s-err">—</div>
    </div>
    <div class="stat-card">
      <div class="label">Dropped</div>
      <div class="value" id="s-drop">—</div>
      <div class="sub">messages</div>
    </div>
    <div class="stat-card">
      <div class="label">Ring Occupancy</div>
      <div class="value" id="s-ring">—</div>
      <div class="sub">max slots seen</div>
    </div>
  </div>

  <!-- Charts -->
  <div class="charts">

    <div class="chart-card full">
      <h3>E2E Latency Percentiles (ns) — rolling window</h3>
      <canvas id="latChart"></canvas>
    </div>

    <div class="chart-card">
      <h3>Throughput — Messages / sec</h3>
      <canvas id="tputChart"></canvas>
    </div>

    <div class="chart-card">
      <h3>Per-Component p95 Latency (ns)</h3>
      <canvas id="compChart"></canvas>
    </div>

    <div class="chart-card full">
      <h3>Per-Symbol p95 Latency (ns)</h3>
      <div id="symTable"></div>
    </div>

  </div>
</main>

<script>
const MAX_POINTS = 60;
const COLORS = { p50:'#22c55e', p95:'#eab308', p99:'#f97316', p999:'#ef4444', mean:'#3b82f6' };
const ts = () => new Date().toLocaleTimeString();

function mkDataset(label, color, dash=[]) {
  return { label, data: [], borderColor: color, backgroundColor: color + '18',
           borderWidth: 2, borderDash: dash, pointRadius: 0, fill: false, tension: 0.3 };
}

// ── Latency chart ──────────────────────────────────────────────────────────
const latCtx = document.getElementById('latChart').getContext('2d');
const latChart = new Chart(latCtx, {
  type: 'line',
  data: {
    labels: [],
    datasets: [
      mkDataset('p50',   COLORS.p50),
      mkDataset('p95',   COLORS.p95),
      mkDataset('p99',   COLORS.p99),
      mkDataset('p99.9', COLORS.p999),
      mkDataset('mean',  COLORS.mean, [4,2]),
    ]
  },
  options: {
    animation: false,
    plugins: { legend: { labels: { color: '#94a3b8', boxWidth: 12 } } },
    scales: {
      x: { ticks: { color: '#475569', maxTicksLimit: 8 }, grid: { color: '#1e2130' } },
      y: { ticks: { color: '#475569' }, grid: { color: '#1e2130' },
           title: { display: true, text: 'nanoseconds', color: '#475569' } }
    }
  }
});

// ── Throughput chart ───────────────────────────────────────────────────────
const tputCtx = document.getElementById('tputChart').getContext('2d');
const tputChart = new Chart(tputCtx, {
  type: 'line',
  data: {
    labels: [],
    datasets: [
      mkDataset('worker msg/s', '#3b82f6'),
      mkDataset('parser msg/s', '#a855f7'),
    ]
  },
  options: {
    animation: false,
    plugins: { legend: { labels: { color: '#94a3b8', boxWidth: 12 } } },
    scales: {
      x: { ticks: { color: '#475569', maxTicksLimit: 8 }, grid: { color: '#1e2130' } },
      y: { ticks: { color: '#475569' }, grid: { color: '#1e2130' } }
    }
  }
});

// ── Component chart ────────────────────────────────────────────────────────
const compCtx = document.getElementById('compChart').getContext('2d');
const compChart = new Chart(compCtx, {
  type: 'bar',
  data: {
    labels: ['ingress → worker', 'parse → worker'],
    datasets: [{ label: 'p95 latency (ns)', data: [0, 0],
      backgroundColor: ['#3b82f688', '#a855f788'],
      borderColor:     ['#3b82f6',   '#a855f7'],
      borderWidth: 1, borderRadius: 4 }]
  },
  options: {
    animation: { duration: 400 },
    plugins: { legend: { display: false } },
    scales: {
      x: { ticks: { color: '#475569' }, grid: { color: '#1e2130' } },
      y: { ticks: { color: '#475569' }, grid: { color: '#1e2130' },
           title: { display: true, text: 'nanoseconds', color: '#475569' } }
    }
  }
});

// ── Helpers ────────────────────────────────────────────────────────────────
function push(chart, label, values) {
  chart.data.labels.push(label);
  values.forEach((v, i) => chart.data.datasets[i].data.push(v));
  if (chart.data.labels.length > MAX_POINTS) {
    chart.data.labels.shift();
    chart.data.datasets.forEach(d => d.shift ? d.shift() : d.data.shift());
  }
  chart.update('none');
}

function fmtNs(ns) {
  if (ns >= 1e9)  return (ns/1e9).toFixed(2)  + ' s';
  if (ns >= 1e6)  return (ns/1e6).toFixed(2)  + ' ms';
  if (ns >= 1e3)  return (ns/1e3).toFixed(1)  + ' µs';
  return ns + ' ns';
}

function fmtNum(n) {
  if (n >= 1e9) return (n/1e9).toFixed(2) + 'B';
  if (n >= 1e6) return (n/1e6).toFixed(2) + 'M';
  if (n >= 1e3) return (n/1e3).toFixed(1) + 'K';
  return String(n);
}

const HEALTH_COLORS = ['#22c55e','#eab308','#ef4444','#64748b'];
const HEALTH_LABELS = ['HEALTHY','DEGRADED','UNHEALTHY','UNKNOWN'];

// ── Symbol table ───────────────────────────────────────────────────────────
function renderSymTable(symbols) {
  if (!symbols || !Object.keys(symbols).length) {
    document.getElementById('symTable').innerHTML = '<p style="color:var(--muted);font-size:.8rem;padding:8px">No symbol data yet.</p>';
    return;
  }
  const sorted = Object.entries(symbols).sort((a,b) => b[1]-a[1]);
  const max = sorted[0][1] || 1;
  const rows = sorted.map(([sym, val]) => {
    const pct = Math.max(2, Math.round(val/max*100));
    const color = val < 1000 ? 'var(--green)' : val < 10000 ? 'var(--yellow)' : val < 100000 ? 'var(--orange)' : 'var(--red)';
    return `<tr>
      <td><strong>${sym}</strong></td>
      <td style="color:${color}">${fmtNs(val)}</td>
      <td><div class="bar-cell"><div class="bar" style="width:${pct}%;background:${color}"></div></div></td>
    </tr>`;
  }).join('');
  document.getElementById('symTable').innerHTML = `
    <table>
      <thead><tr><th>Symbol</th><th>p95 Latency</th><th>Relative</th></tr></thead>
      <tbody>${rows}</tbody>
    </table>`;
}

// ── Poll loop ──────────────────────────────────────────────────────────────
async function poll() {
  try {
    const r = await fetch('/api/metrics');
    const d = await r.json();
    const now = ts();

    // stat cards
    const hColor = HEALTH_COLORS[d.health] || HEALTH_COLORS[3];
    document.getElementById('s-health').textContent = d.health_label;
    document.getElementById('s-health').style.color = hColor;
    document.getElementById('s-msgs').textContent  = fmtNum(d.total_messages);
    document.getElementById('s-tput').textContent  = fmtNum(d.throughput.worker_msg_s);
    document.getElementById('s-err').textContent   = d.error_rate.toFixed(2) + '%';
    document.getElementById('s-err').style.color   = d.error_rate > 10 ? 'var(--red)' : d.error_rate > 1 ? 'var(--yellow)' : 'var(--green)';
    document.getElementById('s-drop').textContent  = d.dropped;
    document.getElementById('s-drop').style.color  = d.dropped > 0 ? 'var(--red)' : 'var(--green)';
    document.getElementById('s-ring').textContent  = d.ring_occupancy_max;

    // latency chart
    push(latChart, now, [d.latency.p50, d.latency.p95, d.latency.p99, d.latency.p999, d.latency.mean]);

    // throughput chart
    push(tputChart, now, [d.throughput.worker_msg_s, d.throughput.parser_msg_s]);

    // component bar
    compChart.data.datasets[0].data = [d.components.ingress_to_worker, d.components.parse_to_worker];
    compChart.update();

    // symbol table
    renderSymTable(d.symbols);

    document.getElementById('last-update').textContent = 'updated ' + now;
  } catch(e) {
    console.warn('poll error', e);
  }
}

// init badge
const badge = document.getElementById('mode-badge');
fetch('/api/mode').then(r=>r.json()).then(d=>{
  badge.textContent = d.demo ? 'DEMO MODE' : 'LIVE';
  badge.className = 'badge ' + (d.demo ? 'demo' : 'live');
});

poll();
setInterval(poll, 3000);
</script>
</body>
</html>
"""

# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):  # silence access log
        pass

    def do_GET(self):
        if self.path == '/' or self.path == '/index.html':
            self._send(200, 'text/html', HTML.encode())
        elif self.path == '/api/metrics':
            data = json.dumps(get_metrics()).encode()
            self._send(200, 'application/json', data)
        elif self.path == '/api/mode':
            data = json.dumps({"demo": DEMO_MODE}).encode()
            self._send(200, 'application/json', data)
        else:
            self._send(404, 'text/plain', b'Not found')

    def _send(self, code, ct, body):
        self.send_response(code)
        self.send_header('Content-Type', ct)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(body)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
if __name__ == '__main__':
    mode = 'DEMO' if DEMO_MODE else 'LIVE'
    print(f'ULMD Dashboard [{mode}] → http://localhost:{PORT}')
    if DEMO_MODE:
        print('  Running with simulated data. Start ULMD and unset ULMD_DEMO_MODE for live data.')
    else:
        print(f'  CSV:     {CSV_PATH}')
        print(f'  Metrics: {WORKER_METRICS}')
        print(f'  Health:  {HEALTH_PATH}')
    HTTPServer(('0.0.0.0', PORT), Handler).serve_forever()
