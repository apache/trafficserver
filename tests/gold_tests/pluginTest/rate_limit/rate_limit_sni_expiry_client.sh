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
# Exercise max_age expiry while a holder occupies the single slot. The sweep must expire
# the queued connection without resuming or rejecting it, and leave the slot counter
# balanced for the holder's release and the probe's reservation.
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

# 1. Holder: reserve the single slot before starting the queued connection.
"${openssl[@]}" <&3 >/dev/null 2>&1 &
holder=$!
wait_for 'Reserving a slot, active entities == 1' 1

# 2. One queued connection: enqueues and stays parked at the ClientHello hook (not killed),
#    so the sweep's max_age expiry -- not a disconnect or a resume -- is what removes it.
"${openssl[@]}" <&3 >/dev/null 2>&1 &
queued=$!
wait_for 'Queueing the VC, we are at capacity' 1

# 3. Keep the holder alive until the sweep actually expires the queued connection.
wait_for 'Queued VC is too old' 1
end_connection "$queued"
queued=""

# 4. End the holder and wait for its slot to be released.
end_connection "$holder"
holder=""
wait_for 'Releasing a slot, active entities ==' 1

# 5. Reserve and release the slot again. An unmatched release during expiry would wrap
#    the counter when the holder closes, causing this reservation to abort ATS.
"${openssl[@]}" <&3 >/dev/null 2>&1 &
probe=$!
wait_for 'Reserving a slot, active entities == 1' 2
end_connection "$probe"
probe=""
wait_for 'Releasing a slot, active entities ==' 2

echo "rate_limit-expiry-done"
