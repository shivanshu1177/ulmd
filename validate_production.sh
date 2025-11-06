#!/bin/bash
set -euo pipefail

echo "ULMD Production Readiness Validation"
echo "==================================="

CHECKS_PASSED=0
TOTAL_CHECKS=0

check() {
    local name="$1"
    local command="$2"
    
    TOTAL_CHECKS=$((TOTAL_CHECKS + 1))
    echo -n "Checking ${name}... "
    
    if eval "${command}" >/dev/null 2>&1; then
        echo "✅ PASS"
        CHECKS_PASSED=$((CHECKS_PASSED + 1))
    else
        local exit_code=$?
        echo "❌ FAIL (exit code: ${exit_code})"
        echo "  Command failed: ${command}"
    fi
}

# Security Checks
echo "Security Validation:"
check "Build system" "[[ -x build.sh ]]"
check "Test system" "[[ -x run_tests.sh ]]"
check "Configuration" "[[ -f config/ulmd.conf ]]"
check "CI/CD pipeline" "[[ -f .github/workflows/ci.yml ]]"

# Functionality Checks  
echo -e "\nFunctionality Validation:"
check "Build completion" "./build.sh"
check "Test suite" "./run_tests.sh"
check "Deployment script" "[[ -x deploy.sh ]]"
check "Monitoring system" "[[ -x scripts/monitor.sh ]]"

# Documentation Checks
echo -e "\nDocumentation Validation:"
check "README" "[[ -f README.md ]]"
check "Backup system" "[[ -x scripts/backup.sh ]]"
check "Benchmarking" "[[ -x scripts/benchmark.sh ]]"

echo -e "\nValidation Summary:"
echo "=================="
echo "Checks passed: ${CHECKS_PASSED}/${TOTAL_CHECKS}"

if [[ ${CHECKS_PASSED} -eq ${TOTAL_CHECKS} ]]; then
    echo "🎉 PRODUCTION READY! All validation checks passed."
    exit 0
else
    echo "❌ NOT PRODUCTION READY. Fix failing checks."
    exit 1
fi