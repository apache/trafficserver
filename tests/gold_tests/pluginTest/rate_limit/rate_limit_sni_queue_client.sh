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
# Deterministically drive the rate_limit SNI limiter's queue-accounting balance bug. A
# queued connection never increments the active-slot counter, but its VCONN_CLOSE always
# decrements it, so one queued connection that closes is a single unmatched decrement.
# With exactly one queued connection there is no way for the sweep to mask it:
#
#   1. holder completes its handshake and holds the single slot (counter = 1);
#   2. one connection enqueues (slot full), then closes cleanly (FIN) while parked;
#   3. the sweep resumes it, its handshake fails and it closes -> one unmatched decrement
#      -> counter 1 -> 0 (the queue is now empty, so no reserve() can rebalance it);
#   4. the holder is closed; its matched decrement lands on the understated counter
#      -> counter 0 -> wraps below zero;
#   5. a probe connection's reserve() observes the wrapped counter and the limiter's
#      release assertion (_active <= _limit) aborts the server.
#
# args: host port sni
set -u
host="$1"
port="$2"
sni="$3"

OSSL="openssl s_client -connect ${host}:${port} -servername ${sni} -quiet -verify_quiet -no_ign_eof"

# 1. Holder: hold the single slot. Its stdin is a FIFO kept open on fd 3, so we end the
#    holder deterministically in step 4 (closing fd 3 -> EOF -> clean TLS close -> FIN).
fifo_dir="$(mktemp -d "${TMPDIR:-/tmp}/rl_holder.XXXXXX")"
fifo="${fifo_dir}/fifo"
mkfifo "$fifo"
${OSSL} <"$fifo" >/dev/null 2>&1 &
exec 3<>"$fifo"
rm -rf "$fifo_dir"
sleep 3 # let the holder reserve the one slot

# 2. One queued connection: enqueues (slot full), then sends a clean FIN ~0.3s later while
#    still parked at the ClientHello hook.
timeout 0.3 ${OSSL} </dev/null >/dev/null 2>&1 &
sleep 2 # >= 2 sweep periods (300ms each): the sweep resumes the queued connection, its
        # handshake fails (EPIPE) and it closes -> one unmatched decrement -> counter 1 -> 0

# 4. End the holder: its matched decrement lands on the already-understated counter.
exec 3>&- # close the FIFO write end -> holder sees EOF -> clean TLS close (FIN)
sleep 2   # let the holder's close run: counter 0 -> wraps below zero

# 5. Probe: its reserve() reads the wrapped counter and trips TSReleaseAssert(_active <= _limit).
timeout 2 ${OSSL} </dev/null >/dev/null 2>&1 || true
sleep 1

echo "rate_limit-queue-crash-done"
