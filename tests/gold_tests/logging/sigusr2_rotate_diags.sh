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

set -eu

if [ "$#" -ne 5 ]; then
  echo "Usage: $0 <python> <handler> <ts_name> <diags_log> <rotated_diags_log>" >&2
  exit 2
fi

python_bin="$1"
handler="$2"
ts_name="$3"
diags_log="$4"
rotated_diags_log="$5"

wait_for_file() {
  path="$1"
  tries="$2"
  attempt=0

  while [ "$attempt" -lt "$tries" ]; do
    if [ -f "$path" ]; then
      return 0
    fi
    sleep 1
    attempt=$((attempt + 1))
  done

  echo "Timed out waiting for file: $path" >&2
  return 1
}

wait_for_contains() {
  path="$1"
  needle="$2"
  tries="$3"
  attempt=0

  while [ "$attempt" -lt "$tries" ]; do
    if [ -f "$path" ] && grep -F "$needle" "$path" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
    attempt=$((attempt + 1))
  done

  echo "Timed out waiting for '$needle' in $path" >&2
  return 1
}

rm -f "$rotated_diags_log"

wait_for_file "$diags_log" 60
wait_for_contains "$diags_log" "traffic server running" 60

mv "$diags_log" "$rotated_diags_log"

# Send the SIGUSR2 signal to the handler to trigger ATS to rotate the log.
"$python_bin" "$handler" --signal SIGUSR2 "$ts_name"

wait_for_file "$diags_log" 60
wait_for_contains "$diags_log" "Reseated diags.log" 60
wait_for_contains "$rotated_diags_log" "traffic server running" 60
