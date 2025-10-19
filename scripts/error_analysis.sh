#!/bin/bash

# ULMD Error Analysis Script
# Analyzes error logs for patterns and trends

ERROR_LOG="/tmp/ulmd_errors.log"

if [ ! -f "$ERROR_LOG" ]; then
    echo "No error log found at $ERROR_LOG"
    exit 1
fi

echo "ULMD Error Analysis Report"
echo "=========================="
echo "Generated: $(date)"
echo

# Total error count
total_errors=$(wc -l < "$ERROR_LOG")
echo "Total errors: $total_errors"
echo

# Errors by component
echo "Errors by component:"
echo "-------------------"
grep -o '\] [^:]*:' "$ERROR_LOG" | sed 's/] //; s/://' | sort | uniq -c | sort -nr
echo

# Recent errors (last 10)
echo "Recent errors (last 10):"
echo "------------------------"
tail -10 "$ERROR_LOG"
echo

# Error patterns with better analysis
echo "Common error patterns:"
echo "---------------------"
if ! grep -o ': .*$' "$ERROR_LOG" 2>/dev/null | sed 's/: //' | sort | uniq -c | sort -nr | head -5; then
    echo "No error patterns found or analysis failed"
fi
echo

# Error frequency analysis
echo "Error frequency analysis:"
echo "------------------------"
echo "Errors in last hour:"
if command -v date >/dev/null 2>&1; then
    one_hour_ago=$(date -d '1 hour ago' '+%s' 2>/dev/null || date -v-1H '+%s' 2>/dev/null || echo "0")
    if [ "$one_hour_ago" != "0" ]; then
        awk -v threshold="$one_hour_ago" 'BEGIN{count=0} /^\[/ {gsub(/[\[\]]/, "", $1); if($1 > threshold) count++} END{print count}' "$ERROR_LOG" 2>/dev/null || echo "Analysis unavailable"
    else
        echo "Date calculation unavailable"
    fi
else
    echo "Date command not available for time-based analysis"
fi

# System health indicators
echo
echo "System health indicators:"
echo "------------------------"
if [ "$total_errors" -eq 0 ]; then
    echo "Status: HEALTHY (no errors recorded)"
elif [ "$total_errors" -lt 10 ]; then
    echo "Status: GOOD (low error count)"
elif [ "$total_errors" -lt 100 ]; then
    echo "Status: WARNING (moderate error count)"
else
    echo "Status: CRITICAL (high error count - investigation required)"
fi

# Recommendations
echo
echo "Recommendations:"
echo "---------------"
if [ "$total_errors" -gt 50 ]; then
    echo "- Review system logs for root cause analysis"
    echo "- Check system resources (CPU, memory, disk)"
    echo "- Verify network connectivity and configuration"
fi
if grep -q "ring.*attach.*fail" "$ERROR_LOG" 2>/dev/null; then
    echo "- Check shared memory configuration and permissions"
fi
if grep -q "socket.*error" "$ERROR_LOG" 2>/dev/null; then
    echo "- Verify network configuration and firewall settings"
fi