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

# Test: independent limiters per remap rule.
# Two remap rules each with limit=1. Saturating one should not affect the other.

ATS_PORT=$1
HOST_A="limit-a.example.com"
HOST_B="limit-b.example.com"

# Hold a slot on rule A (3s origin delay)
curl -s -o /dev/null \
  "http://127.0.0.1:${ATS_PORT}/slow" \
  -H "Host: ${HOST_A}" &

sleep 0.5

# Request to rule B should still pass (independent limiter)
B_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
  "http://127.0.0.1:${ATS_PORT}/fast" \
  -H "Host: ${HOST_B}")

wait

echo "independent=${B_CODE}"
