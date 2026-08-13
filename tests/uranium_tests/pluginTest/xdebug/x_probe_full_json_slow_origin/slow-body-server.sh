#!/bin/bash
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

# A simple server that sends headers and first chunk immediately, then delays
# before sending subsequent chunks. This triggers the xdebug transform bug
# when the transform is called but no body data is available yet.
#
# Usage: slow-body-server.sh <port> <outfile>

PORT=$1
OUTFILE=$2

# Create a named pipe for the response
FIFO=$(mktemp -u)
mkfifo "$FIFO"

# Start nc in background, reading from the fifo
nc -l "$PORT" > "$OUTFILE" < "$FIFO" &
NC_PID=$!

# Open the fifo for writing
exec 3>"$FIFO"

# Wait for the request to arrive (look for empty line ending headers)
while true; do
    if [[ -f "$OUTFILE" ]]; then
        if grep -q $'^\r$' "$OUTFILE" 2>/dev/null || grep -q '^$' "$OUTFILE" 2>/dev/null; then
            break
        fi
    fi
    sleep 0.1
done

# Send headers with chunked encoding
printf "HTTP/1.1 200 OK\r\n" >&3
printf "Content-Type: text/plain\r\n" >&3
printf "Transfer-Encoding: chunked\r\n" >&3
printf "X-Response: slow-chunked\r\n" >&3
printf "\r\n" >&3

# Send first chunk immediately
printf "5\r\n" >&3
printf "hello\r\n" >&3

# Delay before next chunk - this is the key to triggering the bug
# The transform will see more data expected but buffer empty
sleep 2

# Send second chunk
printf "5\r\n" >&3
printf "world\r\n" >&3

# End chunked encoding
printf "0\r\n" >&3
printf "\r\n" >&3

# Close the fifo
exec 3>&-

# Wait for nc to finish
wait $NC_PID 2>/dev/null

# Cleanup
rm -f "$FIFO"
