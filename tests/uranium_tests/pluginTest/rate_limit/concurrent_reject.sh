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

# Test: fire two requests concurrently at the rate limiter.
# First request hits a slow origin (holds the slot for 3s).
# Second request arrives 500ms later and should get 429.

ATS_PORT=$1
HOST="limit.example.com"

curl -s -o /dev/null -w "%{http_code}" \
  "http://127.0.0.1:${ATS_PORT}/slow" \
  -H "Host: ${HOST}" &
SLOW_PID=$!

sleep 0.5

FAST_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
  "http://127.0.0.1:${ATS_PORT}/fast" \
  -H "Host: ${HOST}")

wait $SLOW_PID

echo "fast=${FAST_CODE}"
