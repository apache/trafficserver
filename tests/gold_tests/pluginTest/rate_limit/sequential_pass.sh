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

# Test: sequential requests both pass when slot freed between them.
# With limit=1, serial requests should both get 200 because the
# TXN_CLOSE hook frees the slot before the next request arrives.

ATS_PORT=$1
HOST="limit.example.com"

FIRST=$(curl -s -o /dev/null -w "%{http_code}" \
  "http://127.0.0.1:${ATS_PORT}/slow" \
  -H "Host: ${HOST}")

SECOND=$(curl -s -o /dev/null -w "%{http_code}" \
  "http://127.0.0.1:${ATS_PORT}/fast" \
  -H "Host: ${HOST}")

echo "first=${FIRST} second=${SECOND}"
