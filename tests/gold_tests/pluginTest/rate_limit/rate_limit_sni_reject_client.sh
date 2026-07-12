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
# Drive the rate_limit SNI limiter (limit 1, no queue) through its reject path so the
# consumer-driven SSLNetVConnection teardown is exercised for a rejected handshake:
#   holder completes the handshake and HOLDS the one slot open;
#   a burst of near-simultaneous handshakes then arrives while the slot is taken and,
#         with no queue configured, each is REJECTED with TS_EVENT_ERROR mid-handshake.
# ATS must free every one of these rejected handshake VCs cleanly.
#
# args: host port sni
set -u
host="$1"
port="$2"
sni="$3"

OSSL="openssl s_client -connect ${host}:${port} -servername ${sni} -quiet -verify_quiet -no_ign_eof"

# holder: complete the handshake and hold the single slot for ~5s (slow stdin keeps it open).
(sleep 5) | ${OSSL} >/dev/null 2>&1 &
sleep 2 # let the holder reserve the slot

# Burst of near-simultaneous handshakes against the full limiter; with no queue every one
# is rejected with TS_EVENT_ERROR, so its handshake VC is torn down consumer-driven.
for _ in $(seq 5); do
  timeout 2 ${OSSL} </dev/null >/dev/null 2>&1 &
done

# Let the burst finish and the holder release its slot cleanly.
sleep 4

echo "rate_limit-reject-done"
