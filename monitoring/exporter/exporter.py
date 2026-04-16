#!/usr/bin/env python3
"""
ULMD Prometheus Metrics Exporter

Reads ULMD output files and exposes metrics for Prometheus scraping.

Files consumed:
  /tmp/ulmd_latency.csv          - per-message latency records from worker_risk
  /tmp/worker_risk_metrics.txt   - throughput histogram from worker_risk
  /tmp/parser_metrics.txt        - throughput histogram from parser
  /tmp/parser_health.status      - health status from parser
"""

import os
import csv
import time
import statistics
import io
from pathlib import Path
from prometheus_client import start_http_server, Gauge, CollectorRegistry, REGISTRY
from prometheus_client.core import GaugeMetricFamily, HistogramMetricFamily, REGISTRY as DEFAULT_REGISTRY

# ---------------------------------------------------------------------------
# Configuration (override via environment variables)
# ---------------------------------------------------------------------------
CSV_PATH          = os.environ.get('ULMD_CSV_PATH',            '/tmp/ulmd_latency.csv')
WORKER_METRICS    = os.environ.get('ULMD_WORKER_METRICS_PATH', '/tmp/worker_risk_metrics.txt')
PARSER_METRICS    = os.environ.get('ULMD_PARSER_METRICS_PATH', '/tmp/parser_metrics.txt')
HEALTH_PATH       = os.environ.get('ULMD_HEALTH_PATH',         '/tmp/parser_health.status')
PORT              = int(os.environ.get('ULMD_EXPORTER_PORT',   '9101'))
CSV_TAIL_ROWS     = int(os.environ.get('ULMD_CSV_TAIL_ROWS',   '10000'))

# Latency histogram bucket boundaries (nanoseconds) — mirrors C++ LatencyHistogram
LATENCY_BUCKETS_NS = [1_000, 10_000, 100_000, 1_000_000, 10_000_000,
                      100_000_000, 1_000_000_000, 10_000_000_000, float('inf')]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def tail_file(path: str, n: int) -> list[str]:
    """Return up to the last *n* lines of *path* without loading the whole file."""
    try:
        with open(path, 'rb') as f:
            f.seek(0, 2)
            size = f.tell()
            if size == 0:
                return []
            chunk_size = 65536
            pos = size
            lines: list[bytes] = []
            while len(lines) < n + 2 and pos > 0:
                pos = max(0, pos - chunk_size)
                f.seek(pos)
                chunk = f.read(min(chunk_size, size - pos))
                lines = chunk.splitlines()
            return [l.decode('utf-8', errors='replace') for l in lines[-n:]]
    except (FileNotFoundError, IOError):
        return []


def parse_kv_file(path: str) -> dict[str, str]:
    """Parse a key=value text file into a dict."""
    result: dict[str, str] = {}
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if '=' in line:
                    k, _, v = line.partition('=')
                    result[k.strip()] = v.strip()
    except (FileNotFoundError, IOError):
        pass
    return result


def percentile(data: list[float], p: float) -> float:
    if not data:
        return 0.0
    data_sorted = sorted(data)
    idx = (p / 100) * (len(data_sorted) - 1)
    lo, hi = int(idx), min(int(idx) + 1, len(data_sorted) - 1)
    return data_sorted[lo] + (data_sorted[hi] - data_sorted[lo]) * (idx - lo)


def build_histogram_counts(values: list[float]) -> tuple[list[int], int]:
    """Bucket *values* into LATENCY_BUCKETS_NS, return (counts, total)."""
    counts = [0] * len(LATENCY_BUCKETS_NS)
    for v in values:
        for i, bound in enumerate(LATENCY_BUCKETS_NS):
            if v <= bound:
                counts[i] += 1
                break
    return counts, len(values)


# ---------------------------------------------------------------------------
# Custom Prometheus collector
# ---------------------------------------------------------------------------

class UlmdCollector:
    def collect(self):
        # ── 1. Latency from CSV ──────────────────────────────────────────
        e2e_lat: list[float] = []
        ingress_lat: list[float] = []
        parse_lat: list[float] = []
        symbols: dict[str, list[float]] = {}
        dropped_total = 0
        ring_occ_max = 0

        lines = tail_file(CSV_PATH, CSV_TAIL_ROWS)
        # Skip header row(s) that start with non-numeric data
        data_lines = [l for l in lines if l and l[0].isdigit()]
        for row_str in data_lines:
            parts = row_str.split(',')
            if len(parts) < 15:
                continue
            try:
                sym                     = parts[1][:8]
                lat_ingress_to_worker   = float(parts[7])
                lat_parse_to_worker     = float(parts[8])
                dropped                 = int(parts[13])
                ring_occ                = int(parts[11])

                e2e_lat.append(lat_ingress_to_worker)
                ingress_lat.append(lat_ingress_to_worker)
                parse_lat.append(lat_parse_to_worker)
                symbols.setdefault(sym, []).append(lat_ingress_to_worker)
                dropped_total = max(dropped_total, dropped)
                ring_occ_max = max(ring_occ_max, ring_occ)
            except (ValueError, IndexError):
                continue

        # E2E latency percentiles
        for label, pct, data in [
            ('p50',  50,  e2e_lat),
            ('p95',  95,  e2e_lat),
            ('p99',  99,  e2e_lat),
            ('p999', 99.9, e2e_lat),
        ]:
            g = GaugeMetricFamily(
                f'ulmd_e2e_latency_{label}_nanoseconds',
                f'End-to-end latency {label} in nanoseconds',
            )
            g.add_metric([], percentile(data, pct))
            yield g

        # Mean latency
        g = GaugeMetricFamily('ulmd_e2e_latency_mean_nanoseconds',
                              'End-to-end mean latency in nanoseconds')
        g.add_metric([], statistics.mean(e2e_lat) if e2e_lat else 0.0)
        yield g

        # Component latencies (p95)
        g = GaugeMetricFamily('ulmd_component_latency_p95_nanoseconds',
                              'Per-component p95 latency in nanoseconds',
                              labels=['component'])
        g.add_metric(['ingress_to_worker'], percentile(ingress_lat, 95))
        g.add_metric(['parse_to_worker'],   percentile(parse_lat,   95))
        yield g

        # Per-symbol p95 latency
        g = GaugeMetricFamily('ulmd_symbol_latency_p95_nanoseconds',
                              'Per-symbol p95 e2e latency in nanoseconds',
                              labels=['symbol'])
        for sym, vals in symbols.items():
            g.add_metric([sym], percentile(vals, 95))
        yield g

        # Latency histogram (cumulative, Prometheus convention)
        counts, total = build_histogram_counts(e2e_lat)
        cumulative = []
        running = 0
        bucket_bounds = []
        for i, bound in enumerate(LATENCY_BUCKETS_NS[:-1]):   # skip +Inf
            running += counts[i]
            cumulative.append((str(int(bound)), running))
            bucket_bounds.append(bound)
        running += counts[-1]  # +Inf bucket
        h = HistogramMetricFamily(
            'ulmd_e2e_latency_nanoseconds',
            'End-to-end latency histogram in nanoseconds',
        )
        h.add_metric(
            [],
            buckets=cumulative + [('+Inf', running)],
            sum_value=sum(e2e_lat),
            count_value=total,
        )
        yield h

        # Ring buffer
        g = GaugeMetricFamily('ulmd_ring_occupancy_max_slots',
                              'Maximum observed ring buffer occupancy in slots')
        g.add_metric([], ring_occ_max)
        yield g

        g = GaugeMetricFamily('ulmd_dropped_messages_total',
                              'Total dropped messages')
        g.add_metric([], dropped_total)
        yield g

        g = GaugeMetricFamily('ulmd_csv_rows_sampled',
                              'Number of CSV rows used in this scrape')
        g.add_metric([], len(data_lines))
        yield g

        # ── 2. Throughput from worker_risk metrics file ──────────────────
        wm = parse_kv_file(WORKER_METRICS)
        for metric, field, desc in [
            ('ulmd_worker_messages_per_second', 'messages_per_sec',  'Worker messages per second'),
            ('ulmd_worker_bytes_per_second',    'bytes_per_sec',     'Worker bytes per second'),
            ('ulmd_worker_total_messages',      'total_messages',    'Worker total messages processed'),
            ('ulmd_worker_total_bytes',         'total_bytes',       'Worker total bytes processed'),
            ('ulmd_worker_e2e_latency_avg_ns',  'e2e_latency_avg_ns','Worker average e2e latency (ns)'),
        ]:
            g = GaugeMetricFamily(metric, desc)
            g.add_metric([], float(wm.get(field, 0)))
            yield g

        # Worker e2e latency histogram from metrics file
        raw = wm.get('e2e_latency_histogram', '')
        if raw:
            try:
                buckets_raw = [int(x) for x in raw.split(',') if x.strip()]
                cum, run2 = 0, 0
                cum_buckets = []
                for i, b in enumerate(LATENCY_BUCKETS_NS[:-1]):
                    run2 += buckets_raw[i] if i < len(buckets_raw) else 0
                    cum_buckets.append((str(int(b)), run2))
                if len(buckets_raw) > len(LATENCY_BUCKETS_NS) - 1:
                    run2 += buckets_raw[len(LATENCY_BUCKETS_NS) - 1]
                h2 = HistogramMetricFamily(
                    'ulmd_worker_e2e_latency_nanoseconds',
                    'Worker e2e latency histogram (from metrics file)',
                )
                h2.add_metric(
                    [],
                    buckets=cum_buckets + [('+Inf', run2)],
                    sum_value=float(wm.get('e2e_latency_avg_ns', 0)) * run2,
                    count_value=run2,
                )
                yield h2
            except (ValueError, IndexError):
                pass

        # ── 3. Parser throughput ─────────────────────────────────────────
        pm = parse_kv_file(PARSER_METRICS)
        for metric, field, desc in [
            ('ulmd_parser_messages_per_second', 'messages_per_sec',    'Parser messages per second'),
            ('ulmd_parser_bytes_per_second',    'bytes_per_sec',       'Parser bytes per second'),
            ('ulmd_parser_total_messages',      'total_messages',      'Parser total messages'),
            ('ulmd_parser_parse_latency_avg_ns','parse_latency_avg_ns','Parser average parse latency (ns)'),
        ]:
            g = GaugeMetricFamily(metric, desc)
            g.add_metric([], float(pm.get(field, 0)))
            yield g

        # ── 4. Health status ─────────────────────────────────────────────
        hs = parse_kv_file(HEALTH_PATH)
        status_map = {'HEALTHY': 0, 'DEGRADED': 1, 'UNHEALTHY': 2}
        status_val = status_map.get(hs.get('status', ''), 3)  # 3 = unknown

        g = GaugeMetricFamily('ulmd_health_status',
                              'System health: 0=healthy 1=degraded 2=unhealthy 3=unknown')
        g.add_metric([], status_val)
        yield g

        g = GaugeMetricFamily('ulmd_error_rate_percent',
                              'Error rate percentage')
        g.add_metric([], float(hs.get('error_rate_percent', 0)))
        yield g

        g = GaugeMetricFamily('ulmd_health_messages_processed',
                              'Messages processed (health tracker)')
        g.add_metric([], float(hs.get('messages_processed', 0)))
        yield g

        g = GaugeMetricFamily('ulmd_health_errors_count',
                              'Errors count (health tracker)')
        g.add_metric([], float(hs.get('errors_count', 0)))
        yield g


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    DEFAULT_REGISTRY.register(UlmdCollector())
    start_http_server(PORT)
    print(f'ULMD exporter listening on :{PORT}/metrics')
    print(f'  CSV:            {CSV_PATH}')
    print(f'  Worker metrics: {WORKER_METRICS}')
    print(f'  Parser metrics: {PARSER_METRICS}')
    print(f'  Health:         {HEALTH_PATH}')
    while True:
        time.sleep(10)
