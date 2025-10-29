# ULMD Production Deployment Guide

## ⚠️ CRITICAL: DO NOT DEPLOY UNTIL SECURITY ISSUES ARE FIXED

### Current Status: NOT PRODUCTION READY
- 3 Critical vulnerabilities found
- Multiple High-severity security issues
- Inadequate error handling

## Pre-Deployment Requirements

### 1. Fix Security Issues
```bash
# Fix all Critical and High-severity issues found in code review
# Address format string vulnerabilities
# Fix buffer overflow risks
# Resolve path traversal vulnerabilities
# Fix log injection issues
```

### 2. Validate Fixes
```bash
./validate_production.sh
```

## Deployment Steps (Once Security Issues Fixed)

### Step 1: Pre-Deployment Validation
```bash
# Run comprehensive tests
./run_tests.sh

# Validate build
./scripts/validate_build.sh

# Run security scan
# (Add security scanning tools)
```

### Step 2: Build for Production
```bash
# Clean build
./scripts/clean_build.sh

# Production build
export CXXFLAGS="-O2 -DNDEBUG -std=c++20 -I include"
./build.sh
```

### Step 3: Deploy to Staging
```bash
# Deploy to staging environment
./deploy.sh staging

# Run integration tests
./test_pipeline.sh

# Performance benchmarks
./scripts/benchmark.sh
```

### Step 4: Production Deployment
```bash
# Deploy to production
sudo ./deploy.sh production

# Start services
sudo systemctl start ulmd-ingress
sudo systemctl start ulmd-parser

# Enable auto-start
sudo systemctl enable ulmd-ingress
sudo systemctl enable ulmd-parser
```

### Step 5: Post-Deployment Monitoring
```bash
# Start monitoring
./scripts/monitor.sh

# Check service status
sudo systemctl status ulmd-ingress
sudo systemctl status ulmd-parser

# Monitor logs
tail -f /tmp/ulmd_*.log
```

## CRITICAL: Security Issues Must Be Fixed First

### High Priority Fixes Needed:
1. Format string vulnerabilities in multiple files
2. Buffer overflow risks in worker_risk.cpp
3. Path traversal in config.cpp and health.cpp
4. Log injection vulnerabilities
5. Missing authorization in test_shutdown.cpp

### Recommended Actions:
1. Fix all security vulnerabilities
2. Add comprehensive input validation
3. Implement proper error handling
4. Add security testing to CI/CD
5. Conduct security audit

## DO NOT DEPLOY TO PRODUCTION UNTIL ALL SECURITY ISSUES ARE RESOLVED