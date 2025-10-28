#!/usr/bin/env bash
# Robust TSAN report generator for the project.
# - Builds the TSAN-instrumented binary
# - Runs it with TSAN_OPTIONS pointing at a repo-root prefix
# - Exercises the server with a few client sessions
# - Stops the server and waits for ThreadSanitizer to write its report files
# - Aggregates the report files into `tsan_report.txt` and prints a concise summary

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPORT_PREFIX="$ROOT/tsan_report"
REPORT_FILE="$ROOT/tsan_report.txt"
SERVER_BIN="$ROOT/server_tsan"
SERVER_OUT="$ROOT/server_tsan.out"

echo "[INFO] Project root: $ROOT"

cd "$ROOT"

echo "[INFO] Building TSAN server (make tsan)..."
if ! make tsan >/dev/null 2>&1; then
    echo "[ERROR] make tsan failed"
    exit 2
fi

# cleanup previous artifacts
rm -f "${REPORT_PREFIX}".* "$REPORT_FILE" "$SERVER_OUT" || true

if [ ! -x "$SERVER_BIN" ]; then
    echo "[ERROR] TSAN binary not found at $SERVER_BIN"
    exit 2
fi

echo "[INFO] Starting server_tsan (TSAN will write files with prefix: ${REPORT_PREFIX})"
# Export TSAN_OPTIONS for this process invocation only; ensure absolute path so TSAN writes where we expect
export TSAN_OPTIONS="log_path=${REPORT_PREFIX}"
"$SERVER_BIN" > "$SERVER_OUT" 2>&1 &
TSAN_PID=$!
echo "[INFO] server_tsan pid=$TSAN_PID"

# small helper to run a quick client session
run_quick() {
    timeout 10 bash -c '(
        echo "SIGNUP quickuser quickpass"
        sleep 0.05
        echo "LOGIN quickuser quickpass"
        sleep 0.05
        echo "LIST"
        sleep 0.05
        echo "QUIT"
    ) | "$ROOT"/client_app' >/dev/null 2>&1 || true
}

echo "[INFO] Running quick client sessions..."
for i in 1 2 3 4 5; do
    run_quick
    sleep 0.1
done

echo "[INFO] Stopping server_tsan (pid=$TSAN_PID)"
# prefer graceful INT, but ensure termination if it doesn't exit
kill -INT "$TSAN_PID" 2>/dev/null || true

# wait for process to exit (with timeout)
for _ in {1..50}; do
    if ! kill -0 "$TSAN_PID" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if kill -0 "$TSAN_PID" 2>/dev/null; then
    echo "[WARN] server_tsan did not exit after SIGINT, sending TERM"
    kill -TERM "$TSAN_PID" 2>/dev/null || true
    sleep 0.5
fi
if kill -0 "$TSAN_PID" 2>/dev/null; then
    echo "[WARN] server_tsan still alive, sending KILL"
    kill -KILL "$TSAN_PID" 2>/dev/null || true
fi

echo "[INFO] Waiting for ThreadSanitizer output files..."
# wait for tsan_report.* files to appear (timeout)
for _ in {1..100}; do
    if compgen -G "${REPORT_PREFIX}.*" > /dev/null; then
        break
    fi
    sleep 0.05
done

shopt -s nullglob
reports=("${REPORT_PREFIX}".*)
if [ ${#reports[@]} -eq 0 ]; then
    echo "[INFO] No ThreadSanitizer report files were generated. This typically means TSAN found no warnings."
    echo "[INFO] Dumping server stdout for reference:"
    echo "---- server_tsan.out ----"
    sed -n '1,200p' "$SERVER_OUT" || true
    echo "---- end server_tsan.out ----"
    echo "[OK] No TSAN warnings detected (no report files)."
    exit 0
fi

echo "[INFO] Aggregating ${#reports[@]} TSAN report file(s) into $REPORT_FILE"
rm -f "$REPORT_FILE"
for f in "${reports[@]}"; do
    echo "===== file: $f =====" >> "$REPORT_FILE"
    cat "$f" >> "$REPORT_FILE"
    echo -e "\n" >> "$REPORT_FILE"
done

echo "[INFO] Saved aggregated TSAN report to $REPORT_FILE"

if grep -q "WARNING: ThreadSanitizer" "$REPORT_FILE" 2>/dev/null; then
    echo "[ERROR] ThreadSanitizer reported warnings. Showing the first useful summary block:"
    # show the first warning with a few lines of context
    awk '/WARNING: ThreadSanitizer/{c=1} c && c<60{print; c++} c==60{exit}' "$REPORT_FILE"
    exit 1
else
    echo "[OK] No ThreadSanitizer warnings found in $REPORT_FILE"
    exit 0
fi
#!/bin/bash

# Generate ThreadSanitizer report by running server_tsan and a few client ops
# Run this from the project root or from tests/ — the script will cd to root.

set -e

# Where to place final report
REPORT_FILE="tsan_report.txt"

# Ensure we are in tests/ when started; move to project root
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

echo "[INFO] Project root: $ROOT"

# Build TSAN binary
echo "[INFO] Building TSAN server (make tsan)..."
if ! make tsan; then
    echo "[ERROR] make tsan failed"
    exit 1
fi

# Run server_tsan with TSAN_OPTIONS to create tsan_report.* files.
# Capture both stdout and stderr to files so we always have data to
# aggregate into the final `tsan_report.txt` even when TSAN writes
# nothing (it only reports when it finds races).
TSAN_LOG_PREFIX="tsan_report"
TSAN_STDOUT_FILE="tsan_stdout.txt"
TSAN_STDERR_FILE="tsan_stderr.txt"
# Make TSAN a bit more verbose; verbosity may produce additional output
# on some TSAN builds. Keep the log_path prefix so TSAN can also write
# per-thread log files if it chooses.
export TSAN_OPTIONS="log_path=${TSAN_LOG_PREFIX}:verbosity=1"
echo "[INFO] Running server_tsan and capturing stdout->${TSAN_STDOUT_FILE} stderr->${TSAN_STDERR_FILE}"
./server_tsan >"${TSAN_STDOUT_FILE}" 2>"${TSAN_STDERR_FILE}" &
TSAN_PID=$!

echo "[INFO] Started server_tsan (pid=$TSAN_PID)"
sleep 2

# Run a few quick client interactions to exercise threads
cd tests


run_quick() {
    (
        echo "SIGNUP tsanuser tsanpass"
        sleep 0.05
        echo "LOGIN tsanuser tsanpass"
        sleep 0.05
        echo "LIST"
        sleep 0.05
        echo "QUIT"
    ) | timeout 10 ../client_app > /dev/null 2>&1 || true
}

# Run several quick sessions sequentially
for i in 1 2 3 4 5; do
    run_quick
done

# Also run a number of concurrent sessions to exercise threads and
# increase the chance TSAN will detect races (if any exist).
echo "[INFO] Launching concurrent client sessions to stress the server"
CONCURRENCY=8
for i in $(seq 1 $CONCURRENCY); do
    (
        # each concurrent client sends a mixture of commands
        echo "SIGNUP tsanuser${i} tsanpass"
        sleep 0.02
        echo "LOGIN tsanuser${i} tsanpass"
        sleep 0.02
        echo "LIST"
        sleep 0.02
        echo "QUIT"
    ) | timeout 15 ../client_app > /dev/null 2>&1 &
done

# Wait a short while for the concurrent clients to finish, with a watchdog.
# If any clients remain after the timeout, force-terminate them so the
# script can proceed to aggregate logs instead of hanging indefinitely.
WATCHDOG_SECS=12
echo "[INFO] Waiting up to ${WATCHDOG_SECS}s for concurrent clients to finish"
end=$((SECONDS + WATCHDOG_SECS))
while [ $SECONDS -le $end ]; do
    # any client_app processes still running?
    if pgrep -f "./client_app" >/dev/null 2>&1; then
        sleep 1
    else
        break
    fi
done
if pgrep -f "./client_app" >/dev/null 2>&1; then
    echo "[WARN] Some client_app processes are still running after ${WATCHDOG_SECS}s; killing them"
    pkill -f "./client_app" || true
    sleep 1
fi

# Give server a moment
sleep 1

# Stop TSAN server
echo "[INFO] Stopping server_tsan (pid=$TSAN_PID)"
kill -INT $TSAN_PID 2>/dev/null || kill $TSAN_PID 2>/dev/null || true
wait $TSAN_PID 2>/dev/null || true
sleep 1
# Function to aggregate tsan logs and captured server output into $REPORT_FILE.
aggregate_reports() {
    rc=$?
    # avoid running trap recursively
    trap - INT TERM EXIT

    echo "[INFO] Aggregating TSAN output into $REPORT_FILE"
    cd "$ROOT"
    FOUND=0
    if ls ${TSAN_LOG_PREFIX}.* 1> /dev/null 2>&1; then
        echo "[INFO] Found TSAN log files (${TSAN_LOG_PREFIX}.*), concatenating (excluding $REPORT_FILE)"
        : > $REPORT_FILE
        for f in ${TSAN_LOG_PREFIX}.*; do
            # skip the final report file if it matches the glob
            [ "$f" = "$REPORT_FILE" ] && continue
            echo "===== contents of $f =====" >> $REPORT_FILE
            cat "$f" >> $REPORT_FILE
            echo -e "\n" >> $REPORT_FILE
        done
        echo "[INFO] TSAN report saved to $REPORT_FILE"
        FOUND=1
    fi

    if [ -f "${TSAN_STDOUT_FILE}" ]; then
        if [ $FOUND -eq 0 ]; then
            echo "[INFO] Creating $REPORT_FILE from TSAN captured output"
            : > $REPORT_FILE
        fi
        echo "===== TSAN server stdout (captured) =====" >> $REPORT_FILE
        cat "${TSAN_STDOUT_FILE}" >> $REPORT_FILE
        FOUND=1
    fi

    if [ -f "${TSAN_STDERR_FILE}" ]; then
        if [ $FOUND -eq 0 ]; then
            echo "[INFO] Creating $REPORT_FILE from TSAN captured output"
            : > $REPORT_FILE
        fi
        echo "===== TSAN server stderr (captured) =====" >> $REPORT_FILE
        cat "${TSAN_STDERR_FILE}" >> $REPORT_FILE
        FOUND=1
    fi

    if [ $FOUND -eq 1 ]; then
        if grep -q "WARNING: ThreadSanitizer" $REPORT_FILE 2>/dev/null; then
            echo "[ERROR] ThreadSanitizer warnings found. Summary:"
            grep -A 10 "WARNING: ThreadSanitizer" $REPORT_FILE | head -n 50
            exit 1
        else
            echo "[OK] No ThreadSanitizer warnings found"
            exit 0
        fi
    else
        echo "[WARN] No TSAN output files were generated"
        exit 1
    fi
}

# Ensure we aggregate reports and stop server even if the script is interrupted
trap aggregate_reports INT TERM EXIT
