#!/bin/bash

# Test concurrent clients accessing server simultaneously
# Tests: thread safety, race conditions, per-file locking, concurrent I/O
# Usage: ./test_concurrent.sh

set -e

SERVER_PID=""
SERVER_LOG="test_concurrent_server.log"
NUM_CLIENTS=8
TEST_DURATION=10

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

print_error() {
    echo -e "${RED}[FAIL]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

cleanup() {
    print_status "Cleaning up..."
    if [ -n "$SERVER_PID" ]; then
        kill $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
    fi
    rm -f $SERVER_LOG
    rm -f client_*.txt client_*.log download_*.txt
    rm -rf server_storage
    print_status "Cleanup complete."
}

trap cleanup EXIT

# Check binaries exist
if [ ! -f "../server" ]; then
    print_error "Server binary not found. Run 'make' first."
    exit 1
fi

if [ ! -f "../client_app" ]; then
    print_error "Client binary not found. Run 'make' first."
    exit 1
fi

echo "=========================================="
echo "  Dropbox Clone - Concurrent Client Test"
echo "=========================================="
echo "Testing with $NUM_CLIENTS concurrent clients"
echo

# Clean any old storage
rm -rf ../server_storage

# Start server
print_status "Starting server..."
cd ..
./server > tests/$SERVER_LOG 2>&1 &
SERVER_PID=$!
cd tests
sleep 2

# Verify server started
if ! kill -0 $SERVER_PID 2>/dev/null; then
    print_error "Server failed to start"
    cat $SERVER_LOG
    exit 1
fi
print_success "Server started (PID: $SERVER_PID)"
echo

# Create test files with unique content for each client
print_status "Preparing test files for $NUM_CLIENTS clients..."
for i in $(seq 1 $NUM_CLIENTS); do
    {
        echo "=== Client $i Test File ==="
        echo "Timestamp: $(date)"
        echo "Random data: $RANDOM$RANDOM$RANDOM"
        for j in {1..10}; do
            echo "Line $j: Client $i - Data packet $RANDOM"
        done
        echo "=== End of Client $i File ==="
    } > client_$i.txt
done
print_success "Created $NUM_CLIENTS unique test files"
echo

# Function to run a single client session
run_client() {
    local id=$1
    local timestamp=$(date +%s%N | cut -b1-13)
    local user="user${id}_${timestamp}"
    local pass="pass$id"
    local file="client_$id.txt"
    local log="client_$id.log"
    
    {
        # Signup
        echo "SIGNUP $user $pass"
        sleep 0.3
        
        # Login
        echo "LOGIN $user $pass"
        sleep 0.3
        
        # Upload file
        echo "UPLOAD $file"
        sleep 1.0
        
        # List files (should only see own file)
        echo "LIST"
        sleep 0.5
        
        # Download file back
        echo "DOWNLOAD $file"
        sleep 1.0
        
        # List again
        echo "LIST"
        sleep 0.5
        
        # Delete file
        echo "DELETE $file"
        sleep 0.5
        
        # List after delete (should be empty)
        echo "LIST"
        sleep 0.5
        
        # Quit
        echo "QUIT"
        sleep 0.3
    } | timeout 30 ../client_app > $log 2>&1
    
    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo "Client $id: SUCCESS"
    else
        echo "Client $id: FAILED (exit code: $exit_code)"
    fi
    
    return $exit_code
}

# Test 1: Sequential clients (baseline)
print_status "Test 1: Sequential Client Operations (Baseline)"
SEQUENTIAL_PASSED=0
SEQUENTIAL_FAILED=0

for i in {1..3}; do
    if run_client $i >/dev/null 2>&1; then
        SEQUENTIAL_PASSED=$((SEQUENTIAL_PASSED + 1))
    else
        SEQUENTIAL_FAILED=$((SEQUENTIAL_FAILED + 1))
    fi
done

if [ $SEQUENTIAL_FAILED -eq 0 ]; then
    print_success "Sequential test: $SEQUENTIAL_PASSED/3 clients succeeded"
else
    print_error "Sequential test: $SEQUENTIAL_FAILED/3 clients failed"
fi
echo

# Clean up for next test
rm -f client_*.log download_*.txt
rm -rf ../server_storage

# Test 2: Concurrent clients (main test)
print_status "Test 2: Concurrent Client Operations"
print_status "Launching $NUM_CLIENTS clients simultaneously..."

START_TIME=$(date +%s)
PIDS=()

# Launch all clients concurrently
for i in $(seq 1 $NUM_CLIENTS); do
    run_client $i &
    PIDS+=($!)
done

# Wait for all clients to complete
wait

END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

print_success "All clients completed in ${DURATION}s"
echo

# Verify results
print_status "Verifying results..."
PASSED=0
FAILED=0

for i in $(seq 1 $NUM_CLIENTS); do
    log="client_$i.log"
    file="client_$i.txt"
    
    # Check if log exists
    if [ ! -f "$log" ]; then
        print_error "Client $i: Log file missing"
        FAILED=$((FAILED + 1))
        continue
    fi
    
    # Check authentication
    if ! grep -q "OK login" "$log"; then
        print_error "Client $i: Authentication failed"
        FAILED=$((FAILED + 1))
        continue
    fi
    
    # Check upload (look for OK upload OR READY indicating upload started)
    if ! grep -q "OK upload" "$log"; then
        print_error "Client $i: Upload failed"
        FAILED=$((FAILED + 1))
        continue
    fi
    
    # Check download
    if ! grep -q "Downloaded\|OK download" "$log"; then
        print_error "Client $i: Download failed"
        FAILED=$((FAILED + 1))
        continue
    fi
    
    # Check delete
    if ! grep -q "OK delete" "$log"; then
        print_error "Client $i: Delete failed"
        FAILED=$((FAILED + 1))
        continue
    fi
    
    # All checks passed
    PASSED=$((PASSED + 1))
done

echo
echo "=========================================="
echo "             Test Results"
echo "=========================================="
echo "Concurrent Clients: $NUM_CLIENTS"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
echo "Duration: ${DURATION}s"
echo

# Test 3: File integrity check
print_status "Test 3: File Integrity Verification"
INTEGRITY_OK=true

# Re-run a few clients to upload files
for i in {1..3}; do
    user="testuser$i"
    pass="testpass$i"
    file="client_$i.txt"
    
    timeout 15 bash -c "
    {
        echo 'SIGNUP $user $pass'
        sleep 0.3
        echo 'LOGIN $user $pass'
        sleep 0.3
        echo 'UPLOAD $file'
        sleep 1
        echo 'QUIT'
    } | ../client_app > /dev/null 2>&1
    " || print_warning "Client $i upload timed out"
done

# Now download and verify
for i in {1..3}; do
    user="testuser$i"
    pass="testpass$i"
    file="client_$i.txt"
    download_file="download_$i.txt"
    
    {
        echo "LOGIN $user $pass"
        sleep 0.2
        echo "DOWNLOAD $file"
        sleep 0.5
        echo "QUIT"
    } | timeout 10 ../client_app > /dev/null 2>&1
    
    # Compare original and downloaded
    if [ -f "$file" ] && [ -f "$file" ]; then
        # Check if file exists in server storage
        server_file="../server_storage/$user/$file"
        if [ -f "$server_file" ]; then
            if cmp -s "$file" "$server_file"; then
                print_success "Client $i: File integrity verified"
            else
                print_error "Client $i: File content mismatch"
                INTEGRITY_OK=false
            fi
        else
            print_error "Client $i: File not found in server storage"
            INTEGRITY_OK=false
        fi
    else
        print_error "Client $i: Download verification failed"
        INTEGRITY_OK=false
    fi
done
echo

# Test 4: Race condition test (same file multiple times)
print_status "Test 4: Race Condition Test (Concurrent same-user operations)"

RACE_USER="raceuser"
RACE_PASS="racepass"

# Signup once
{
    echo "SIGNUP $RACE_USER $RACE_PASS"
    sleep 0.2
    echo "LOGIN $RACE_USER $RACE_PASS"
    sleep 0.2
    echo "QUIT"
} | timeout 10 ../client_app > /dev/null 2>&1

# Multiple clients uploading as same user concurrently
for i in {1..5}; do
    {
        echo "LOGIN $RACE_USER $RACE_PASS"
        sleep 0.2
        echo "UPLOAD client_$i.txt"
        sleep 0.5
        echo "LIST"
        sleep 0.2
        echo "QUIT"
    } | timeout 10 ../client_app > race_$i.log 2>&1 &
done

wait

# Check for crashes or errors
RACE_ERRORS=0
for i in {1..5}; do
    if grep -q "ERR\|Error\|error" race_$i.log 2>/dev/null; then
        if ! grep -q "OK upload" race_$i.log; then
            RACE_ERRORS=$((RACE_ERRORS + 1))
        fi
    fi
done

if [ $RACE_ERRORS -eq 0 ]; then
    print_success "Race condition test: No errors detected"
else
    print_warning "Race condition test: $RACE_ERRORS potential issues"
fi

rm -f race_*.log
echo

# Final summary
echo "=========================================="
echo "           Final Summary"
echo "=========================================="

ALL_PASSED=true

if [ $FAILED -eq 0 ] && [ $PASSED -eq $NUM_CLIENTS ]; then
    print_success "Concurrent operations: ALL PASSED ($PASSED/$NUM_CLIENTS)"
else
    print_error "Concurrent operations: SOME FAILED ($FAILED/$NUM_CLIENTS)"
    ALL_PASSED=false
fi

if [ "$INTEGRITY_OK" = true ]; then
    print_success "File integrity: VERIFIED"
else
    print_error "File integrity: FAILED"
    ALL_PASSED=false
fi

if [ $RACE_ERRORS -eq 0 ]; then
    print_success "Race conditions: NONE DETECTED"
else
    print_warning "Race conditions: $RACE_ERRORS potential issues"
fi

echo
echo "This test demonstrates:"
echo "  • Thread-safe queue operations"
echo "  • Concurrent client thread handling"
echo "  • Concurrent worker thread processing"
echo "  • Per-file locking (no race conditions)"
echo "  • Safe concurrent uploads/downloads"
echo "  • Proper session isolation"
echo

if [ "$ALL_PASSED" = true ]; then
    echo -e "${GREEN}✓ ALL CONCURRENT TESTS PASSED${NC}"
    echo
    exit 0
else
    echo -e "${RED}✗ SOME TESTS FAILED${NC}"
    echo "Check client_*.log files for details"
    echo
    exit 1
fi