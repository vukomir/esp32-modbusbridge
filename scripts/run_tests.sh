#!/bin/bash

# Test Runner Script for Inverter Monitoring System
# This script runs all unit tests and generates a comprehensive report

set -e  # Exit on any error

echo "🧪 Inverter Monitoring System - Test Runner"
echo "==========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test results
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Function to run a single test
run_test() {
    local test_name=$1
    echo -e "${BLUE}🔧 Running $test_name tests...${NC}"
    
    if pio test -f $test_name -v; then
        echo -e "${GREEN}✅ $test_name tests PASSED${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}❌ $test_name tests FAILED${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo ""
}

# Check if PlatformIO is installed
if ! command -v pio &> /dev/null; then
    echo -e "${RED}❌ PlatformIO CLI not found. Please install PlatformIO.${NC}"
    exit 1
fi

echo -e "${YELLOW}📋 Running comprehensive test suite...${NC}"
echo ""

# Core Library Tests
echo -e "${BLUE}🔧 Core Libraries${NC}"
run_test "test_config"
run_test "test_esplogger"

# Communication Library Tests  
echo -e "${BLUE}🔧 Communication Libraries${NC}"
run_test "test_modbus_client"
run_test "test_mqtt_client"
run_test "test_wifi_manager"

# Device Library Tests
echo -e "${BLUE}🔧 Device Libraries${NC}"
run_test "test_inverter_factory"
run_test "test_solplanet_asw"
run_test "test_hiking_dds238"

# System Library Tests
echo -e "${BLUE}🔧 System Libraries${NC}"
run_test "test_poller"

# Generate Test Report
echo "=========================================="
echo -e "${BLUE}📊 TEST SUMMARY${NC}"
echo "=========================================="
echo "Total Test Suites: $TOTAL_TESTS"
echo -e "Passed: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed: ${RED}$FAILED_TESTS${NC}"

if [ $FAILED_TESTS -eq 0 ]; then
    echo ""
    echo -e "${GREEN}🎉 ALL TESTS PASSED! 🎉${NC}"
    echo -e "${GREEN}System is ready for deployment.${NC}"
    exit 0
else
    echo ""
    echo -e "${RED}⚠️  SOME TESTS FAILED ⚠️${NC}"
    echo -e "${RED}Please review failed tests before deployment.${NC}"
    exit 1
fi
