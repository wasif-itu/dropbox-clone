#!/bin/bash

# Test single client flow: signup -> login -> upload -> list -> download -> delete
# Usage: ./test_single_client.sh

set -e

SERVER_PID=""
SERVER_LOG="test_server.log"
TEST_FILE="test_upload.txt"
DOWNLOAD_FILE="test_download.txt"
CLIENT_SCRIPT="client_commands.txt"

cleanup() {
    echo "Cleaning up..."
    if [ -n "$SERVER_PID" ]; then
        kill $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
    fi
    rm -f $SERVER_LOG $TEST_FILE $DOWNLOAD_FILE $CLIENT_SCRIPT
    rm -rf server_storage
    echo "Cleanup complete."
}

trap cleanup EXIT

# Check if server exists
if [ ! -f "../server" ]; then
    echo "ERROR: Server binary not found. Run 'make' first."
    exit 1
fi

if [ ! -f "../client_app" ]; then
    echo "ERROR: Client binary not found. Run 'make' first."
    exit 1
fi

echo "=== Dropbox Clone - Single Client Test ==="
echo

# Clean any old storage
rm -rf ../server_storage

# Create test file
echo "This is a test file for upload." > $TEST_FILE
echo "It contains multiple lines." >> $TEST_FILE
echo "Line 3." >> $TEST_FILE

# Start server in background
echo "[1/7] Starting server..."
cd ..
./server > tests/$SERVER_LOG 2>&1 &
SERVER_PID=$!
cd tests
sleep 2

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "ERROR: Server failed to start. Check $SERVER_LOG"
    cat $SERVER_LOG
    exit 1
fi
echo "    Server started (PID: $SERVER_PID)"

# Create client command script
cat > $CLIENT_SCRIPT << 'EOF'
SIGNUP testuser testpass
LOGIN testuser testpass
UPLOAD test_upload.txt
LIST
DOWNLOAD test_upload.txt
DELETE test_upload.txt
LIST
QUIT
EOF

echo "[2/7] Testing SIGNUP..."
echo "SIGNUP testuser testpass" | ../client_app 2>&1 | grep -q "OK signup"
if [ $? -eq 0 ]; then
    echo "    ✓ SIGNUP successful"
else
    echo "    ✗ SIGNUP failed"
    exit 1
fi

echo "[3/7] Testing LOGIN..."
(echo "LOGIN testuser testpass"; sleep 1) | timeout 5 ../client_app 2>&1 | grep -q "OK login"
if [ $? -eq 0 ]; then
    echo "    ✓ LOGIN successful"
else
    echo "    ✗ LOGIN failed"
    exit 1
fi

echo "[4/7] Testing UPLOAD..."
# Full session: login then upload
{
    echo "LOGIN testuser testpass"
    sleep 0.5
    echo "UPLOAD $TEST_FILE"
    sleep 1
    echo "QUIT"
} | timeout 10 ../client_app > /dev/null 2>&1

# Check if file exists in server storage
if [ -f "../server_storage/testuser/$TEST_FILE" ]; then
    echo "    ✓ UPLOAD successful (file stored)"
else
    echo "    ✗ UPLOAD failed (file not found in server storage)"
    exit 1
fi

echo "[5/7] Testing LIST..."
{
    echo "LOGIN testuser testpass"
    sleep 0.5
    echo "LIST"
    sleep 0.5
    echo "QUIT"
} | timeout 10 ../client_app 2>&1 | grep -q "$TEST_FILE"
if [ $? -eq 0 ]; then
    echo "    ✓ LIST successful (file visible)"
else
    echo "    ✗ LIST failed"
    exit 1
fi

echo "[6/7] Testing DOWNLOAD..."
{
    echo "LOGIN testuser testpass"
    sleep 0.5
    echo "DOWNLOAD $TEST_FILE"
    sleep 1
    echo "QUIT"
} | timeout 10 ../client_app > /dev/null 2>&1

if [ -f "$TEST_FILE" ]; then
    if cmp -s "$TEST_FILE" "../server_storage/testuser/$TEST_FILE"; then
        echo "    ✓ DOWNLOAD successful (file matches original)"
    else
        echo "    ✗ DOWNLOAD failed (file content mismatch)"
        exit 1
    fi
else
    echo "    ✗ DOWNLOAD failed (file not downloaded)"
    exit 1
fi

echo "[7/7] Testing DELETE..."
{
    echo "LOGIN testuser testpass"
    sleep 0.5
    echo "DELETE $TEST_FILE"
    sleep 0.5
    echo "QUIT"
} | timeout 10 ../client_app > /dev/null 2>&1

if [ ! -f "../server_storage/testuser/$TEST_FILE" ]; then
    echo "    ✓ DELETE successful (file removed)"
else
    echo "    ✗ DELETE failed (file still exists)"
    exit 1
fi

echo
echo "=== All Tests Passed! ==="
echo
echo "Server log available at: $SERVER_LOG"