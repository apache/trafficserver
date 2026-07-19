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
# Exercise the max_age EXPIRY branch of the rate_limit SNI queue accounting. A queued
# connection never reserves a slot; when the sweep expires it (max_age), it must be
# detached so its close does not release a slot it never held. Otherwise the expiry is an
# unmatched decrement of the active-slot counter, and -- combined with the holder's own
# close -- the counter wraps below zero and the limiter's release assertion aborts ATS.
#
#   1. holder completes its handshake and holds the single slot (counter = 1);
#   2. one connection enqueues (slot full) and stays parked -- it is NOT disconnected, so
#      only the sweep's max_age expiry removes it;
#   3. after max_age the sweep errors it out -> (unfixed) unmatched decrement -> counter 1->0;
#   4. the holder is closed; its matched decrement lands on the understated counter -> wrap;
#   5. a probe connection's reserve() observes the wrapped counter and the assertion aborts.
#
# args: host port sni
set -u
host="$1"
port="$2"
sni="$3"

OSSL="openssl s_client -connect ${host}:${port} -servername ${sni} -quiet -verify_quiet -no_ign_eof"

# 1. Holder: hold the single slot. Its stdin is a FIFO on fd 3 so we end it in step 4.
fifo_dir="$(mktemp -d "${TMPDIR:-/tmp}/rl_holder.XXXXXX")"
fifo="${fifo_dir}/fifo"
mkfifo "$fifo"
${OSSL} <"$fifo" >/dev/null 2>&1 &
exec 3<>"$fifo"
rm -rf "$fifo_dir"
sleep 3 # let the holder reserve the one slot

# 2. One queued connection: enqueues and stays parked at the ClientHello hook (not killed),
#    so the sweep's max_age expiry -- not a disconnect or a resume -- is what removes it.
${OSSL} </dev/null >/dev/null 2>&1 &
queued=$!
sleep 3 # > max_age (1s) + sweeps: the expiry path errors the queued connection out

# 4. End the holder: its matched decrement lands on the (unfixed) understated counter.
exec 3>&-
sleep 2

# 5. Probe: its reserve() reads the counter; if it wrapped, the release assertion aborts.
timeout 2 ${OSSL} </dev/null >/dev/null 2>&1 || true
kill "${queued}" 2>/dev/null || true
sleep 1

echo "rate_limit-expiry-done"
