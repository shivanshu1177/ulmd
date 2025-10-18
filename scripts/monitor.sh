#!/bin/bash
set -euo pipefail

METRICS_DIR="/tmp"
ALERT_THRESHOLD_ERROR_RATE=10
ALERT_THRESHOLD_LATENCY_MS=100
ALERT_LOG="/tmp/ulmd_alerts.log"

monitor_health() {
    local component="$1"
    local health_file="${METRICS_DIR}/${component}_health.status"
    
    if [[ ! -f "${health_file}" ]]; then
        echo "ALERT: ${component} health file missing" | tee -a "${ALERT_LOG}"
        return 1
    fi
    
    local status=$(grep "status=" "${health_file}" | cut -d= -f2)
    local error_rate=$(grep "error_rate_percent=" "${health_file}" | cut -d= -f2 2>/dev/null || echo "0")
    
    if [[ "${status}" != "HEALTHY" ]]; then
        echo "ALERT: ${component} status is ${status}" | tee -a "${ALERT_LOG}"
    fi
    
    if (( $(echo "${error_rate} > ${ALERT_THRESHOLD_ERROR_RATE}" | bc -l 2>/dev/null || echo "0") )); then
        echo "ALERT: ${component} error rate ${error_rate}% exceeds threshold" | tee -a "${ALERT_LOG}"
    fi
}

monitor_performance() {
    local component="$1"
    local metrics_file="${METRICS_DIR}/${component}_metrics.txt"
    
    if [[ ! -f "${metrics_file}" ]]; then
        return 0
    fi
    
    local e2e_avg=$(grep "e2e_latency_avg_ns=" "${metrics_file}" | cut -d= -f2 2>/dev/null || echo "0")
    local latency_ms=$(echo "scale=2; ${e2e_avg} / 1000000" | bc -l 2>/dev/null || echo "0")
    
    if (( $(echo "${latency_ms} > ${ALERT_THRESHOLD_LATENCY_MS}" | bc -l 2>/dev/null || echo "0") )); then
        echo "ALERT: ${component} latency ${latency_ms}ms exceeds threshold" | tee -a "${ALERT_LOG}"
    fi
}

echo "ULMD System Monitor - $(date)"
echo "============================="

for component in parser worker_risk ingress; do
    echo "Monitoring ${component}..."
    monitor_health "${component}" || true
    monitor_performance "${component}" || true
done

echo "Monitoring complete"