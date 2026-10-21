#!/bin/sh
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

# Regression test for the queue drain bug (Finding #106).
#
# With limit=1 and queue=5, fire 3 requests concurrently against a slow
# origin (3s delay). Correct behavior:
#   - Request A gets the slot, holds it for 3s
#   - Requests B and C are queued
#   - After A completes (~3s), queue handler gives B the slot
#   - After B completes (~6s total), queue handler gives C the slot
#   - Total wall time: ~9s (3 sequential slow requests)
#
# With the old bug (reserve() != RESERVED / != FULL):
#   - Request A gets the slot
#   - Queue handler immediately resumes B and C WITHOUT a valid reservation
#   - B and C run concurrently with A (all finish around ~3s)
#   - Total wall time: ~3s
#
# We detect the bug by measuring wall time. If all 3 finish in under 5s,
# the limiter was bypassed. Correct behavior takes >= 6s (at least 2
# sequential slow-origin round trips for the queued requests).

ATS_PORT=$1
HOST="queued.example.com"

START=$(date +%s)

# Fire 3 requests concurrently
curl -s -o /dev/null -w "a=%{http_code}\n" \
  "http://127.0.0.1:${ATS_PORT}/slow" \
  -H "Host: ${HOST}" &
PID_A=$!

sleep 0.3

curl -s -o /dev/null -w "b=%{http_code}\n" \
  "http://127.0.0.1:${ATS_PORT}/slow" \
  -H "Host: ${HOST}" &
PID_B=$!

sleep 0.3

curl -s -o /dev/null -w "c=%{http_code}\n" \
  "http://127.0.0.1:${ATS_PORT}/slow" \
  -H "Host: ${HOST}" &
PID_C=$!

wait $PID_A
wait $PID_B
wait $PID_C

END=$(date +%s)
ELAPSED=$((END - START))

# With correct limiting: >= 6s (2 queued requests each wait for a slot)
# With the bug: ~3s (all run concurrently, bypassing the limit)
if [ "$ELAPSED" -ge 6 ]; then
  echo "timing=correct elapsed=${ELAPSED}s"
else
  echo "timing=bypassed elapsed=${ELAPSED}s"
fi
