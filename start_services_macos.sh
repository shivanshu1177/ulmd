#!/bin/bash

# ULMD macOS Service Startup Script
# Usage: ./start_services_macos.sh

set -e

echo "Starting ULMD services on macOS..."

# Check if build directory exists
if [ ! -d "build" ]; then
    echo "Error: build directory not found. Run ./build.sh first."
    exit 1
fi

# Check if config exists
if [ ! -f "config/ulmd.conf" ]; then
    echo "Error: config/ulmd.conf not found."
    exit 1
fi

# Clean up any existing ring buffer first
echo "Cleaning up existing ring buffer..."
./build/ringctl destroy --name ulmd_ring 2>/dev/null || true

# Create ring buffer
echo "Creating ring buffer..."
./build/ringctl create --name ulmd_ring --slots 1024 --elem 128

# Start ingress service
echo "Starting ingress service..."
nohup ./build/ingress --config config/ulmd.conf --port 12345 --tsc-hz 3000000000 > /tmp/ulmd_ingress.log 2>&1 &
INGRESS_PID=$!
echo "Ingress started with PID: $INGRESS_PID"

# Start parser service
echo "Starting parser service..."
nohup ./build/parser --ring ulmd_ring --tsc-hz 3000000000 > /tmp/ulmd_parser.log 2>&1 &
PARSER_PID=$!
echo "Parser started with PID: $PARSER_PID"

# Save PIDs for later cleanup
echo $INGRESS_PID > /tmp/ulmd_ingress.pid
echo $PARSER_PID > /tmp/ulmd_parser.pid

echo "✅ ULMD services started successfully!"
echo "Logs: /tmp/ulmd_ingress.log, /tmp/ulmd_parser.log"
echo "PIDs: $INGRESS_PID (ingress), $PARSER_PID (parser)"
echo ""
echo "To stop services: ./stop_services_macos.sh"
echo "To monitor: ./scripts/monitor.sh"