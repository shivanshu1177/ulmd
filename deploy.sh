#!/bin/bash
set -euo pipefail

ENVIRONMENT="${1:-staging}"
BUILD_DIR="${BUILD_DIR:-build}"
DEPLOY_DIR="/opt/ulmd"
SERVICE_USER="ulmd"

echo "ULMD Deployment Script"
echo "====================="
echo "Environment: ${ENVIRONMENT}"
echo "Build directory: ${BUILD_DIR}"
echo "Deploy directory: ${DEPLOY_DIR}"
echo

# Validate build exists
if [[ ! -d "${BUILD_DIR}" ]]; then
    echo "❌ Build directory not found. Run ./build.sh first"
    exit 1
fi

# Run tests before deployment
echo "Running pre-deployment tests..."
if ! ./run_tests.sh; then
    echo "❌ Tests failed. Deployment aborted"
    exit 1
fi

# Create deployment directory
echo "Creating deployment directory..."
if ! sudo mkdir -p "${DEPLOY_DIR}/bin" "${DEPLOY_DIR}/config" "${DEPLOY_DIR}/logs" "${DEPLOY_DIR}/scripts"; then
    echo "❌ Failed to create deployment directories"
    exit 1
fi

# Verify directories were created
for dir in "${DEPLOY_DIR}/bin" "${DEPLOY_DIR}/config" "${DEPLOY_DIR}/logs" "${DEPLOY_DIR}/scripts"; do
    if [[ ! -d "$dir" ]]; then
        echo "❌ Directory $dir was not created"
        exit 1
    fi
done

# Copy binaries
echo "Deploying binaries..."
sudo cp "${BUILD_DIR}"/* "${DEPLOY_DIR}/bin/"
sudo cp scripts/*.sh "${DEPLOY_DIR}/scripts/"

# Set permissions
sudo chown -R "${SERVICE_USER}:${SERVICE_USER}" "${DEPLOY_DIR}" 2>/dev/null || true
sudo chmod +x "${DEPLOY_DIR}/bin"/*
sudo chmod +x "${DEPLOY_DIR}/scripts"/*

# Create systemd service files
echo "Creating systemd services..."
sudo tee /etc/systemd/system/ulmd-ingress.service > /dev/null << 'EOF'
[Unit]
Description=ULMD Ingress Service
After=network.target

[Service]
Type=simple
User=ulmd
ExecStart=/opt/ulmd/bin/ingress --config /opt/ulmd/config/ulmd.conf
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo tee /etc/systemd/system/ulmd-parser.service > /dev/null << 'EOF'
[Unit]
Description=ULMD Parser Service
After=network.target ulmd-ingress.service

[Service]
Type=simple
User=ulmd
ExecStart=/opt/ulmd/bin/parser --config /opt/ulmd/config/ulmd.conf
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

# Reload systemd
sudo systemctl daemon-reload

echo "✅ Deployment completed successfully"
echo "To start services: sudo systemctl start ulmd-ingress ulmd-parser"
echo "To enable auto-start: sudo systemctl enable ulmd-ingress ulmd-parser"