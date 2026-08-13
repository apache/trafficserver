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

# Test: queue drain behavior (exercises the fixed reserve() loop).
# With limit=1 and queue=5, the second request gets queued (not rejected).
# After the first request completes (origin delay), the queue handler
# resumes the second request which then succeeds with 200.

ATS_PORT=$1
HOST="queued.example.com"

# First request: holds the single slot for 3s (origin delay)
curl -s -o /dev/null -w "slow=%{http_code}\n" \
  "http://127.0.0.1:${ATS_PORT}/slow" \
  -H "Host: ${HOST}" &
SLOW_PID=$!

# Wait for first request to reach the origin and hold the slot
sleep 0.5

# Second request: should be queued (not rejected), then resumed after
# the first completes and the queue handler runs (every 300ms).
FAST_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
  "http://127.0.0.1:${ATS_PORT}/fast" \
  -H "Host: ${HOST}")

wait $SLOW_PID

echo "queued=${FAST_CODE}"
