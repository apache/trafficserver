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
# Drive the rate_limit SNI limiter's queue-then-resume path with exactly one queued connection,
# and check that the active-slot counter stays balanced and the server survives.
#
#   1. holder completes its handshake and holds the single slot (counter = 1);
#   2. one connection enqueues because the slot is full, then closes while parked;
#   3. the sweep reserves a slot and resumes a queued connection;
#   4. the holder is closed and releases its slot;
#   5. a probe connection reserves the freed slot.
#
# Against the plugin before 508c1bea26 this aborts the server: the sweep resumed a queued
# connection without a reservation, whose close then decremented the counter unmatched until it
# wrapped and reserve() tripped TSReleaseAssert(_active <= _limit). The test asserts the counter
# never wraps and no signal is logged, so it pins that fix as well as this change.
#
# args: host port sni
set -u
host="$1"
port="$2"
sni="$3"

OSSL="openssl s_client -connect ${host}:${port} -servername ${sni} -quiet -no_ign_eof"

# Run a command in the background and terminate it after a deadline. coreutils "timeout" is not
# available everywhere (notably macOS), so do it with sleep and kill.
run_for() {
  deadline="$1"
  shift
  "$@" &
  target=$!
  (
    sleep "${deadline}"
    kill -TERM "${target}" 2>/dev/null
  ) &
}

# 1. Holder: hold the single slot. Its stdin is a FIFO kept open on fd 3, so we end the
#    holder deterministically in step 4 (closing fd 3 -> EOF -> clean TLS close -> FIN).
fifo_dir="$(mktemp -d "${TMPDIR:-/tmp}/rl_holder.XXXXXX")"
fifo="${fifo_dir}/fifo"
mkfifo "$fifo"
${OSSL} <"$fifo" >/dev/null 2>&1 &
exec 3<>"$fifo"
rm -rf "$fifo_dir"
sleep 3 # let the holder reserve the one slot

# 2. One queued connection: enqueues because the slot is full, then closes while still parked
#    at the ClientHello hook.
run_for 0.3 sh -c "${OSSL} </dev/null >/dev/null 2>&1"
sleep 2 # >= 2 sweep periods (300ms each), so the sweep runs while the connection is queued

# 4. End the holder, releasing its slot.
exec 3>&- # close the FIFO write end -> holder sees EOF -> clean TLS close (FIN)
sleep 2

# 5. Probe: reserve() must succeed against a balanced counter rather than tripping the
#    TSReleaseAssert(_active <= _limit) that a wrapped counter causes.
run_for 2 sh -c "${OSSL} </dev/null >/dev/null 2>&1"
sleep 3

echo "rate_limit-queue-crash-done"
