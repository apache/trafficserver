#!/bin/bash
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License
#
# Benchmark parallel vs sequential ssl_multicert.config loading.
#
# Generates N self-signed certs, then starts ATS twice:
#   1. concurrency=1 (sequential)
#   2. concurrency=<threads> (parallel)
# Compares loading times from diags.log timestamps.
#
# Usage:
#   sudo ./benchmark_parallel_cert_loading.sh [NUM_CERTS] [NUM_THREADS]
#
# Defaults: 200 certs, 8 threads
#
# Prerequisites:
#   - ATS installed (traffic_server, traffic_layout in PATH or at ATS_PREFIX)
#   - openssl CLI available

NUM_CERTS="${1:-200}"
NUM_THREADS="${2:-8}"
ATS_PREFIX="${ATS_PREFIX:-/opt/local/trafficserver}"
WORK_DIR="$(mktemp -d /tmp/ats-cert-bench.XXXXXX)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
NC='\033[0m'

cleanup() {
    if [[ -f "$WORK_DIR/ts.pid" ]]; then
        kill "$(cat "$WORK_DIR/ts.pid")" 2>/dev/null || true
    fi
    echo -e "\n${YELLOW}Benchmark artifacts in: $WORK_DIR${NC}"
}
trap cleanup EXIT

echo -e "${BOLD}=== ATS Parallel Cert Loading Benchmark ===${NC}"
echo "  Certs:   $NUM_CERTS"
echo "  Threads: $NUM_THREADS"
echo "  Workdir: $WORK_DIR"
echo ""

# ─────────────────────────────────────────────
# Step 1: Generate a CA and N self-signed certs
# ─────────────────────────────────────────────
echo -e "${BOLD}Generating $NUM_CERTS certificates...${NC}"

SSL_DIR="$WORK_DIR/ssl"
mkdir -p "$SSL_DIR"

openssl req -x509 -newkey rsa:2048 -keyout "$SSL_DIR/ca.key" -out "$SSL_DIR/ca.pem" \
    -days 1 -nodes -subj "/CN=Benchmark CA" 2>/dev/null

generate_cert() {
    local i=$1
    local domain="bench-${i}.example.com"
    openssl req -newkey rsa:2048 -keyout "$SSL_DIR/cert-${i}.key" -out "$SSL_DIR/cert-${i}.csr" \
        -nodes -subj "/CN=${domain}" 2>/dev/null
    openssl x509 -req -in "$SSL_DIR/cert-${i}.csr" -CA "$SSL_DIR/ca.pem" -CAkey "$SSL_DIR/ca.key" \
        -set_serial "$((1000 + i))" -out "$SSL_DIR/cert-${i}.pem" -days 1 2>/dev/null
    rm -f "$SSL_DIR/cert-${i}.csr"
}

BATCH_SIZE=20
for ((i = 0; i < NUM_CERTS; i++)); do
    generate_cert "$i" &
    if (( (i + 1) % BATCH_SIZE == 0 )); then
        wait
    fi
done
wait

echo -e "${GREEN}  Generated $NUM_CERTS certs${NC}"

# ─────────────────────────────────────────────
# Step 2: Create ATS config layout with runroot
# ─────────────────────────────────────────────
echo -e "${BOLD}Setting up ATS config...${NC}"

CONF_DIR="$WORK_DIR/config"
LOG_DIR="$WORK_DIR/log"
RUN_DIR="$WORK_DIR/runtime"
CACHE_DIR="$WORK_DIR/cache"
mkdir -p "$CONF_DIR" "$LOG_DIR" "$RUN_DIR" "$CACHE_DIR"

cat > "$WORK_DIR/runroot.yaml" <<YAML
prefix: $WORK_DIR
bindir: $ATS_PREFIX/bin
sysconfdir: $CONF_DIR
localstatedir: $RUN_DIR
runtimedir: $RUN_DIR
logdir: $LOG_DIR
cachedir: $CACHE_DIR
YAML

MULTICERT="$CONF_DIR/ssl_multicert.config"
: > "$MULTICERT"
for ((i = 0; i < NUM_CERTS; i++)); do
    echo "ssl_cert_name=cert-${i}.pem ssl_key_name=cert-${i}.key" >> "$MULTICERT"
done
echo "dest_ip=* ssl_cert_name=cert-0.pem ssl_key_name=cert-0.key" >> "$MULTICERT"

echo -e "${GREEN}  ssl_multicert.config: $((NUM_CERTS + 1)) lines${NC}"

echo "map / http://127.0.0.1:1/" > "$CONF_DIR/remap.config"
echo "$CACHE_DIR 64M" > "$CONF_DIR/storage.config"
touch "$CONF_DIR/hosting.config"
touch "$CONF_DIR/ip_allow.yaml"
touch "$CONF_DIR/plugin.config"

# ─────────────────────────────────────────────
# Step 3: Helper to run ATS and extract load time
# ─────────────────────────────────────────────
run_benchmark() {
    local concurrency=$1
    local label=$2
    local port=$3
    local diags_log="$LOG_DIR/diags.log"

    rm -f "$LOG_DIR"/*.log "$RUN_DIR"/* 2>/dev/null

    cat > "$CONF_DIR/records.yaml" <<YAML
records:
  ssl:
    server:
      cert:
        path: $SSL_DIR
      private_key:
        path: $SSL_DIR
      multicert:
        concurrency: $concurrency
        exit_on_load_fail: 0
  http:
    server_ports: '${port}:ssl'
    wait_for_cache: 0
    cache:
      http: 0
  hostdb:
    host_file:
      path: ''
  body_factory:
    template_sets_dir: $ATS_PREFIX/etc/trafficserver/body_factory
YAML

    "$ATS_PREFIX/bin/traffic_server" \
        --run-root="$WORK_DIR" \
        >/dev/null 2>&1 &
    local ts_pid=$!
    echo "$ts_pid" > "$WORK_DIR/ts.pid"

    # Wait for ATS to fully initialize (or timeout after 120s)
    local waited=0
    while true; do
        if [[ -f "$diags_log" ]] && grep -q "fully initialized" "$diags_log" 2>/dev/null; then
            break
        fi
        if ! kill -0 "$ts_pid" 2>/dev/null; then
            echo -e "${RED}  ATS process died${NC}"
            wait "$ts_pid" 2>/dev/null || true
            return 1
        fi
        sleep 0.5
        waited=$((waited + 1))
        if ((waited > 240)); then
            echo -e "${RED}  Timeout waiting for ATS${NC}"
            kill "$ts_pid" 2>/dev/null || true
            wait "$ts_pid" 2>/dev/null || true
            return 1
        fi
    done

    sleep 0.5
    kill "$ts_pid" 2>/dev/null || true
    wait "$ts_pid" 2>/dev/null || true
    rm -f "$WORK_DIR/ts.pid"

    # Extract timestamps from diags.log
    local start_line end_line
    start_line=$(grep "ssl_multicert.config loading" "$diags_log" | grep -v "finished" | head -1) || true
    end_line=$(grep "ssl_multicert.config finished loading" "$diags_log" | head -1) || true

    if [[ -z "$start_line" || -z "$end_line" ]]; then
        echo -e "${RED}  Could not find loading markers in diags.log${NC}"
        echo "  start_line: '$start_line'"
        echo "  end_line:   '$end_line'"
        grep "ssl_multicert" "$diags_log" 2>/dev/null | sed 's/^/    /' || true
        return 1
    fi

    local start_ts end_ts
    start_ts=$(echo "$start_line" | grep -oE '[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]+') || true
    end_ts=$(echo "$end_line" | grep -oE '[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]+') || true

    if [[ -z "$start_ts" || -z "$end_ts" ]]; then
        echo -e "${RED}  Could not parse timestamps${NC}"
        echo "  start: $start_line"
        echo "  end:   $end_line"
        return 1
    fi

    local start_ms end_ms
    start_ms=$(echo "$start_ts" | awk -F'[:.]' '{printf "%d", ($1*3600 + $2*60 + $3)*1000 + $4}')
    end_ms=$(echo "$end_ts" | awk -F'[:.]' '{printf "%d", ($1*3600 + $2*60 + $3)*1000 + $4}')

    local duration_ms=$((end_ms - start_ms))

    echo -e "${GREEN}  $label: ${BOLD}${duration_ms}ms${NC}"
    echo "    Start: $start_ts"
    echo "    End:   $end_ts"

    eval "${label}_ms=$duration_ms"
}

# ─────────────────────────────────────────────
# Step 4: Run benchmarks
# ─────────────────────────────────────────────
echo ""
echo -e "${BOLD}Running sequential loading (concurrency=1)...${NC}"
run_benchmark 1 "sequential" 18443

echo ""
echo -e "${BOLD}Running parallel loading (concurrency=$NUM_THREADS)...${NC}"
run_benchmark "$NUM_THREADS" "parallel" 18444

# ─────────────────────────────────────────────
# Step 5: Report
# ─────────────────────────────────────────────
echo ""
echo -e "${BOLD}=== Results ===${NC}"
echo "  Certs: $NUM_CERTS"
echo "  Threads: $NUM_THREADS"

if [[ -n "${sequential_ms:-}" && -n "${parallel_ms:-}" && "$sequential_ms" -gt 0 ]]; then
    speedup=$(awk "BEGIN {printf \"%.1fx\", $sequential_ms / $parallel_ms}")
    saved=$((sequential_ms - parallel_ms))
    echo -e "  Sequential: ${sequential_ms}ms"
    echo -e "  Parallel:   ${parallel_ms}ms"
    echo -e "  ${BOLD}Speedup:    ${GREEN}${speedup} (saved ${saved}ms)${NC}"
else
    echo -e "  ${RED}Could not compute speedup${NC}"
fi
