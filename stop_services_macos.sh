#!/bin/bash

# ULMD macOS Service Stop Script
# Usage: ./stop_services_macos.sh

echo "Stopping ULMD services on macOS..."

# Stop ingress service
if [ -f "/tmp/ulmd_ingress.pid" ]; then
    INGRESS_PID=$(cat /tmp/ulmd_ingress.pid)
    if kill -0 $INGRESS_PID 2>/dev/null; then
        kill $INGRESS_PID
        echo "Stopped ingress service (PID: $INGRESS_PID)"
    fi
    rm -f /tmp/ulmd_ingress.pid
fi

# Stop parser service
if [ -f "/tmp/ulmd_parser.pid" ]; then
    PARSER_PID=$(cat /tmp/ulmd_parser.pid)
    if kill -0 $PARSER_PID 2>/dev/null; then
        kill $PARSER_PID
        echo "Stopped parser service (PID: $PARSER_PID)"
    fi
    rm -f /tmp/ulmd_parser.pid
fi

# Clean up ring buffer
echo "Cleaning up ring buffer..."
./build/ringctl destroy --name ulmd_ring 2>/dev/null || true

echo "✅ ULMD services stopped successfully!"