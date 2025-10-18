#!/bin/bash

# ULMD Health Monitor Script
# Checks health status files and sends alerts

HEALTH_DIR="/tmp"
ALERT_LOG="/tmp/ulmd_alerts.log"

check_service_health() {
    local service=$1
    local status_file="$HEALTH_DIR/${service}_health.status"
    
    if [ ! -f "$status_file" ]; then
        echo "$(date): ALERT - $service health file missing" >> "$ALERT_LOG"
        return 1
    fi
    
    # Check if file is recent (updated within last 30 seconds)
    if [ $(( $(date +%s) - $(stat -f %m "$status_file" 2>/dev/null || stat -c %Y "$status_file" 2>/dev/null || echo 0) )) -gt 30 ]; then
        echo "$(date): ALERT - $service health file stale" >> "$ALERT_LOG"
        return 1
    fi
    
    # Check status
    local status=$(grep "^status=" "$status_file" | cut -d= -f2)
    case "$status" in
        "HEALTHY")
            echo "$service: HEALTHY"
            ;;
        "DEGRADED")
            echo "$(date): WARNING - $service is DEGRADED" >> "$ALERT_LOG"
            echo "$service: DEGRADED"
            ;;
        "UNHEALTHY")
            echo "$(date): ALERT - $service is UNHEALTHY" >> "$ALERT_LOG"
            echo "$service: UNHEALTHY"
            return 1
            ;;
        *)
            echo "$(date): ALERT - $service has unknown status: $status" >> "$ALERT_LOG"
            return 1
            ;;
    esac
    
    return 0
}

# Check all services
echo "ULMD Health Check - $(date)"
echo "================================"

overall_status=0

for service in parser ingress; do
    if ! check_service_health "$service"; then
        overall_status=1
    fi
done

if [ $overall_status -eq 0 ]; then
    echo "Overall system status: HEALTHY"
else
    echo "Overall system status: UNHEALTHY"
    echo "Check $ALERT_LOG for details"
fi

exit $overall_status