#!/usr/bin/env bash

set -euo pipefail

# Defaults
MOD="${MOD:-./nvme_test.ko}"
CXL_SET="${CXL_SET:-5}"
TIMEOUT_SEC="${TIMEOUT_SEC:-120}"     # adjust if 1M vectors take longer
LOG_DIR="${LOG_DIR:-./out}"
LOG_FILE="${LOG_FILE:-${LOG_DIR}/last_run.log}"

usage() {
  cat <<EOF
Usage: $0 [--ko PATH] [--cxl_set N] [--timeout SEC] [--logdir DIR]

Env overrides:
  MOD=./nvme_test.ko  CXL_SET=5  TIMEOUT_SEC=120  LOG_DIR=./out

Examples:
  sudo $0
  sudo $0 --cxl_set 5
  sudo MOD=./build/nvme_test.ko TIMEOUT_SEC=300 $0
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ko) MOD="$2"; shift 2;;
    --cxl_set) CXL_SET="$2"; shift 2;;
    --timeout) TIMEOUT_SEC="$2"; shift 2;;
    --logdir) LOG_DIR="$2"; shift 2;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown arg: $1"; usage; exit 2;;
  esac
done

if [[ $EUID -ne 0 ]]; then
  echo "Please run as root (sudo)." >&2
  exit 1
fi

[[ -f "$MOD" ]] || { echo "Module not found: $MOD" >&2; exit 1; }
mkdir -p "$LOG_DIR"
: > "$LOG_FILE"

MOD_NAME="nvme_test"
START_EPOCH="$(date +%s)"

# Clean previous instance
if lsmod | awk '{print $1}' | grep -q "^${MOD_NAME}$"; then
  echo "[run] Removing existing module ${MOD_NAME}..."
  rmmod "${MOD_NAME}" || true
fi

# Start dmesg tail from this moment
echo "[run] Tailing dmesg since ${START_EPOCH} -> ${LOG_FILE}"
( dmesg --since "@${START_EPOCH}" --follow || true ) | tee -a "$LOG_FILE" &
DMESG_PID=$!

# Ensure cleanup
cleanup() {
  set +e
  if lsmod | awk '{print $1}' | grep -q "^${MOD_NAME}$"; then
    echo "[run] Removing module ${MOD_NAME}..."
    rmmod "${MOD_NAME}" || true
  fi
  if ps -p "$DMESG_PID" >/dev/null 2>&1; then
    kill "$DMESG_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

echo "[run] Inserting ${MOD} (cxl_set=${CXL_SET})..."
insmod "$MOD" "cxl_set=${CXL_SET}"

# Wait for success/failure or timeout
echo "[run] Waiting up to ${TIMEOUT_SEC}s for result markers..."
SUCCESS_PAT='L2 stream result:|Wrote L2 results to'
FAIL_PAT='failed rc=|timeout|Failed to write|error|ERROR|Failed to open'

deadline=$((SECONDS + TIMEOUT_SEC))
while (( SECONDS < deadline )); do
  if grep -E -q "${SUCCESS_PAT}" "$LOG_FILE"; then
    echo "[run] Success marker detected."
    break
  fi
  if grep -E -iq "${FAIL_PAT}" "$LOG_FILE"; then
    echo "[run] Failure marker detected (see ${LOG_FILE})."
    break
  fi
  sleep 1
done

if (( SECONDS >= deadline )); then
  echo "[run] Timeout after ${TIMEOUT_SEC}s (see ${LOG_FILE})."
fi

# Module is auto-removed by trap; give it a moment to flush logs
sleep 1
echo "[run] Done. Log: ${LOG_FILE}"
