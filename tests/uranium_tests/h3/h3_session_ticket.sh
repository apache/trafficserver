#!/usr/bin/env bash
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not
#  use this file except in compliance with the License.  You may
#  obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

set -euo pipefail

session_option=$1
session_file=$2
port=$3

set +e
sleep 1 | timeout 5 openssl s_client \
  -quic \
  -alpn h3 \
  -connect "127.0.0.1:${port}" \
  -servername foo.com \
  "${session_option}" "${session_file}" \
  -brief \
  -ign_eof
status=${PIPESTATUS[1]}
set -e

if [[ ${status} -ne 0 && ${status} -ne 1 && ${status} -ne 124 ]]; then
  exit "${status}"
fi

test -s "${session_file}"
