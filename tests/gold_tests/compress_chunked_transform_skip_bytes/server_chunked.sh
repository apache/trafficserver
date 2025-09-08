#!/usr/bin/env bash
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

# Usage: server_chunked.sh <port> <outfile>

response() {
  # Wait for end of Request message (blank line) recorded into $outfile.
  while true; do
    if [ -f "$outfile" ]; then
      if tr '\r\n' '=!' < "$outfile" | grep '=!=!' > /dev/null; then
        break
      fi
    fi
    sleep 0.1
  done

  # Send a cacheable, chunked response.
  printf "HTTP/1.1 200 OK\r\n"
  printf "Connection: close\r\n"
  printf "Cache-Control: public, max-age=60\r\n"
  printf "Content-Type: text/plain\r\n"
  printf "Transfer-encoding: chunked\r\n\r\n"

  # Body: one valid chunk (larger to ensure compression) then terminator.
  # Use a repeated, compressible string and compute the correct chunk size.
  body=$(printf 'This is a small compressible body.%.0s' {1..128})
  size_hex=$(printf "%X" ${#body})
  printf "%s\r\n%s\r\n0\r\n\r\n" "$size_hex" "$body"
}

port=$1
outfile=$2

response | nc -l "$port" > "$outfile"

