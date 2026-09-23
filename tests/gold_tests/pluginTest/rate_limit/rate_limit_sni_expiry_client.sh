#!/usr/bin/env bash
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
#  limitations under the License.
#
# Allow one active connection and keep it open. A second connection waits in a queue.
# Once it has waited longer than max_age, the plugin must remove it from the queue.
# Close the first connection, then check that the plugin accepts a third without crashing ATS.
#
# The expired connection's VCONN_CLOSE is outside this test's coverage. The current TLS
# core parks the VC after the error reenable and does not observe the client's FIN, so
# terminating s_client below only cleans up the client process. A missing detachment in
# the expiry branch therefore does not produce an unmatched release during this test.
#
# args: host port sni traffic_out
set -eu
host="$1"
port="$2"
sni="$3"
traffic_out="$4"

openssl=(openssl s_client -connect "${host}:${port}" -servername "$sni" -quiet)
WAIT_LIMIT=30
holder=""
queued=""
probe=""
fifo_dir=""

# Bound each wait, but advance as soon as the plugin reports the required event.
wait_for() {
  local needle="$1" count="$2" seen end=$((SECONDS + WAIT_LIMIT))

  while :; do
    seen=$(grep -c -F -- "$needle" "$traffic_out" 2>/dev/null || true)
    if [ "${seen:-0}" -ge "$count" ]; then
      return 0
    fi
    if [ "$SECONDS" -ge "$end" ]; then
      echo "timed out waiting for occurrence ${count} of '${needle}' (saw ${seen:-0})" >&2
      tail -n 20 "$traffic_out" >&2
      exit 1
    fi
    sleep 0.1
  done
}

# Close openssl itself rather than relying on version-dependent stdin EOF handling.
end_connection() {
  local pid="$1" end=$((SECONDS + 3))

  {
    kill -TERM "$pid" 2>/dev/null || true
    while kill -0 "$pid" 2>/dev/null; do
      if [ "$SECONDS" -ge "$end" ]; then
        kill -KILL "$pid" 2>/dev/null || true
        break
      fi
      sleep 0.1
    done
    wait "$pid" || true
  } 2>/dev/null
}

cleanup() {
  local pid

  for pid in ${holder} ${queued} ${probe}; do
    end_connection "$pid"
  done
  if [ -n "$fifo_dir" ]; then
    rm -rf "$fifo_dir"
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Open the FIFO before launching any child. Clients inherit the descriptor, so removing
# the path cannot race their startup. Its read/write end keeps stdin idle until teardown.
fifo_dir="$(mktemp -d "${TMPDIR:-/tmp}/rl_holder.XXXXXX")"
mkfifo "${fifo_dir}/fifo"
exec 3<>"${fifo_dir}/fifo"
rm -rf "$fifo_dir"
fifo_dir=""

# 1. Keep the first connection open to reach the plugin's connection limit.
"${openssl[@]}" <&3 >/dev/null 2>&1 &
holder=$!
wait_for 'Reserving a slot, active entities == 1' 1

# 2. Start a second connection and wait for it to enter the queue.
"${openssl[@]}" <&3 >/dev/null 2>&1 &
queued=$!
wait_for 'Queueing the VC, we are at capacity' 1

# 3. Keep the first connection open until the second exceeds max_age and is removed.
wait_for 'Queued VC is too old' 1
end_connection "$queued"
queued=""

# 4. Close the first connection and wait for the plugin to record its closure.
end_connection "$holder"
holder=""
wait_for 'Releasing a slot, active entities ==' 1

# 5. Check that the plugin accepts a third connection without crashing ATS.
"${openssl[@]}" <&3 >/dev/null 2>&1 &
probe=$!
wait_for 'Reserving a slot, active entities == 1' 2
end_connection "$probe"
probe=""
wait_for 'Releasing a slot, active entities ==' 2

echo "rate_limit-expiry-done"
