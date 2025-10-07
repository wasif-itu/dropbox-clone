#!/bin/bash

# Master test script for Dropbox Clone Phase 1
# Runs all verification tests in sequence

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_header() {
    echo
    echo "=========================================="
    echo "$1"
    echo "=========================================="
    echo
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

cleanup() {
    echo
    print_warning "Cleaning up any running servers..."
    pkill -f "./server" 2>/dev/null || true
    pkill -f "./server_tsan" 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT

print_header "Dropbox Clone - Complete Test Suite"

# Check if we're in the right directory
if [ ! -f "Makefile" ]; then
    print_error "Makefile not found. Run this script from project root."
    exit 1
fi

# Step 1: Clean build
print_header "Step 1: Clean Build"
make clean
if make all; then
    print_success "Build successful"
else                                                                                                                                            
    print_error "Build failed"                                                                                                                                                                                                                                                                                                                                                                                                                              
    exit 1
fi

# Step 2: Single client test
print_header "Step 2: Single Client Functional Test"
cd tests
if ./test_single_client.sh; then
    print_success "Single client test passed"
else
    print_error "Single client test failed"
    cd ..
    exit 1
fi
cd ..

# Step 3: Concurrent client test
print_header "Step 3: Concurrent Client Test"
cd tests
if ./test_concurrent.sh; then
    print_success "Concurrent client test passed"
else
    print_error "Concurrent client test failed"
    cd ..
    exit 1
fi
cd ..

# Step 4: Valgrind memory check
print_header "Step 4: Valgrind Memory Leak Check"
print_warning "Starting server under Valgrind (will take ~30 seconds)..."

# Start server under valgrind in background
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --log-file=valgrind_report.txt \
         --error-exitcode=1 \
         ./server > /dev/null 2>&1 &
VALGRIND_PID=$!

sleep 3

# Run quick test
cd tests
(
    echo "SIGNUP valuser valpass"
    sleep 0.5
    echo "LOGIN valuser valpass"
    sleep 0.5
    echo "LIST"
    sleep 0.5
    echo "QUIT"
) | timeout 10 ../client_app > /dev/null 2>&1 || true
cd ..

# Stop server gracefully
kill -INT $VALGRIND_PID 2>/dev/null || true
wait $VALGRIND_PID 2>/dev/null || true

sleep 2

# Check valgrind results
if grep -q "All heap blocks were freed" valgrind_report.txt && \
   grep -q "ERROR SUMMARY: 0 errors" valgrind_report.txt; then
    print_success "Valgrind: No memory leaks detected"
    echo "  Full report: valgrind_report.txt"
else
    print_error "Valgrind: Memory issues detected"
    echo
    echo "=== Valgrind Summary ==="
    grep -A 5 "LEAK SUMMARY" valgrind_report.txt || true
    grep "ERROR SUMMARY" valgrind_report.txt || true
    echo
    print_warning "See valgrind_report.txt for details"
    exit 1
fi

# Step 5: ThreadSanitizer race detection
print_header "Step 5: ThreadSanitizer Race Detection"
print_warning "Building with ThreadSanitizer..."

if make tsan; then
    print_success "ThreadSanitizer build successful"
else
    print_error "ThreadSanitizer build failed"
    exit 1
fi

print_warning "Running server with TSAN (will take ~30 seconds)..."

# Run TSAN server
export TSAN_OPTIONS="log_path=tsan_report"
./server_tsan > /dev/null 2>&1 &
TSAN_PID=$!

sleep 3

# Run concurrent operations
cd tests
for i in {1..3}; do
    (
        echo "SIGNUP tsanuser$i pass$i"
        sleep 0.3
        echo "LOGIN tsanuser$i pass$i"
        sleep 0.3
        echo "LIST"
        sleep 0.3
        echo "QUIT"
    ) | timeout 10 ../client_app > /dev/null 2>&1 &
done
wait

cd ..

# Stop TSAN server
kill -INT $TSAN_PID 2>/dev/null || true
wait $TSAN_PID 2>/dev/null || true

sleep 2

# Check TSAN results
if ls tsan_report.* 1> /dev/null 2>&1; then
    if grep -q "WARNING: ThreadSanitizer" tsan_report.* 2>/dev/null; then
        print_error "ThreadSanitizer: Data races detected!"
        echo
        echo "=== Race Condition Summary ==="
        grep -A 10 "WARNING: ThreadSanitizer" tsan_report.* | head -30
        echo
        print_warning "See tsan_report.* files for details"
        exit 1
    else
        print_success "ThreadSanitizer: No data races detected"
        echo "  Full report: tsan_report.*"
    fi
else
    print_success "ThreadSanitizer: No races (no warnings generated)"
fi

# Final summary
print_header "Test Suite Complete"
echo
print_success "All tests passed successfully!"
echo
echo "Summary:"
echo "  ✓ Build: OK"
echo "  ✓ Single client functional test: PASSED"
echo "  ✓ Concurrent client test: PASSED"
echo "  ✓ Valgrind memory check: CLEAN"
echo "  ✓ ThreadSanitizer race check: CLEAN"
echo
echo "Deliverables ready:"
echo "  1. Source code (src/)"
echo "  2. Makefile"
echo "  3. README.md"
echo "  4. Test scripts (tests/)"
echo "  5. Valgrind report (valgrind_report.txt)"
echo "  6. TSAN report (tsan_report.*)"
echo "  7. Phase 1 report (create from template)"
echo
print_success "Phase 1 requirements complete!"
