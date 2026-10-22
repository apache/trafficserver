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
"""Abandon client TLS handshakes while the async engine is paused.

ATS may observe the peer disconnect only after the async job resumes, so
this client alone does not establish that server-side teardown happens
while an eventfd is still registered. It exercises abandoned handshakes,
not a deterministic regression for mid-job eventfd deregistration.
"""

import socket
import ssl
import sys
import time

port = int(sys.argv[1])
iterations = int(sys.argv[2]) if len(sys.argv) > 2 else 25

ctx = ssl._create_unverified_context()

for _ in range(iterations):
    try:
        raw = socket.create_connection(("127.0.0.1", port), timeout=5)
    except OSError:
        continue
    try:
        tls = ctx.wrap_socket(raw, do_handshake_on_connect=False, server_hostname="example.com")
        # Drive the handshake far enough that ATS enters the async pause, then
        # bail out quickly so the close lands inside the engine's 2s window.
        tls.settimeout(0.4)
        try:
            tls.do_handshake()
        except (ssl.SSLWantReadError, ssl.SSLWantWriteError, ssl.SSLError, socket.timeout, OSError):
            pass
        # Abort: close hard while the async job is still in flight.
        try:
            tls.close()
        except OSError:
            pass
    except OSError:
        try:
            raw.close()
        except OSError:
            pass
    time.sleep(0.05)

print("sent {0} aborted handshakes".format(iterations))
