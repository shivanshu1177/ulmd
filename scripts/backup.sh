#!/bin/bash
set -euo pipefail

BACKUP_DIR="/opt/ulmd/backups"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BACKUP_NAME="ulmd_backup_${TIMESTAMP}"

echo "ULMD Backup System"
echo "=================="
echo "Creating backup: ${BACKUP_NAME}"

# Create backup directory
mkdir -p "${BACKUP_DIR}/${BACKUP_NAME}"

# Backup configuration
echo "Backing up configuration..."
cp -r config/ "${BACKUP_DIR}/${BACKUP_NAME}/"

# Backup logs
echo "Backing up logs..."
mkdir -p "${BACKUP_DIR}/${BACKUP_NAME}/logs"
cp /tmp/ulmd_*.log "${BACKUP_DIR}/${BACKUP_NAME}/logs/" 2>/dev/null || true

# Backup metrics
echo "Backing up metrics..."
mkdir -p "${BACKUP_DIR}/${BACKUP_NAME}/metrics"
cp /tmp/*_metrics.txt "${BACKUP_DIR}/${BACKUP_NAME}/metrics/" 2>/dev/null || true
cp /tmp/*_health.status "${BACKUP_DIR}/${BACKUP_NAME}/metrics/" 2>/dev/null || true

# Create archive
echo "Creating archive..."
cd "${BACKUP_DIR}"
tar -czf "${BACKUP_NAME}.tar.gz" "${BACKUP_NAME}/"
rm -rf "${BACKUP_NAME}/"

# Cleanup old backups (keep last 7 days)
find "${BACKUP_DIR}" -name "ulmd_backup_*.tar.gz" -mtime +7 -delete 2>/dev/null || true

echo "✅ Backup completed: ${BACKUP_DIR}/${BACKUP_NAME}.tar.gz"