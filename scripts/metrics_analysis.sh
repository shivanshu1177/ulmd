#!/bin/bash

# ULMD Performance Metrics Analysis Script

METRICS_DIR="/tmp"

analyze_metrics() {
    local component=$1
    local metrics_file="$METRICS_DIR/${component}_metrics.txt"
    
    if [ ! -f "$metrics_file" ]; then
        echo "$component: No metrics file found"
        return 1
    fi
    
    echo "$component Performance Metrics:"
    echo "$(printf '%.0s-' {1..40})"
    
    # Parse latency metrics with error checking
    if ! parse_avg=$(grep "parse_latency_avg_ns=" "$metrics_file" 2>/dev/null | cut -d= -f2); then
        parse_avg=""
    fi
    if ! e2e_avg=$(grep "e2e_latency_avg_ns=" "$metrics_file" 2>/dev/null | cut -d= -f2); then
        e2e_avg=""
    fi
    
    if [ -n "$parse_avg" ] && [ "$parse_avg" -gt 0 ]; then
        echo "Parse latency avg: $(echo "scale=2; $parse_avg / 1000" | bc -l) μs"
    fi
    
    if [ -n "$e2e_avg" ] && [ "$e2e_avg" -gt 0 ]; then
        echo "E2E latency avg: $(echo "scale=2; $e2e_avg / 1000" | bc -l) μs"
    fi
    
    # Throughput metrics
    msg_rate=$(grep "messages_per_sec=" "$metrics_file" | cut -d= -f2)
    byte_rate=$(grep "bytes_per_sec=" "$metrics_file" | cut -d= -f2)
    
    if [ -n "$msg_rate" ]; then
        echo "Message rate: $msg_rate msg/sec"
    fi
    
    if [ -n "$byte_rate" ]; then
        echo "Throughput: $(echo "scale=2; $byte_rate / 1024 / 1024" | bc -l) MB/sec"
    fi
    
    # Latency distribution
    parse_hist=$(grep "parse_latency_histogram=" "$metrics_file" | cut -d= -f2)
    if [ -n "$parse_hist" ]; then
        echo "Parse latency distribution:"
        IFS=',' read -ra buckets <<< "$parse_hist"
        labels=("0-1μs" "1-10μs" "10-100μs" "100μs-1ms" "1-10ms" "10-100ms" "100ms-1s" "1-10s" "10s+" "invalid")
        
        for i in "${!buckets[@]}"; do
            if [ "${buckets[i]}" -gt 0 ]; then
                echo "  ${labels[i]}: ${buckets[i]}"
            fi
        done
    fi
    
    echo
}

echo "ULMD Performance Analysis Report"
echo "==============================="
echo "Generated: $(date)"
echo

# Analyze all components
for component in parser worker_risk; do
    analyze_metrics "$component"
done