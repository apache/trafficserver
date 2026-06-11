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
"""Abort TLS handshakes while the async engine is mid-job.

The sample async engine pauses each handshake for two seconds. This client
opens a TLS connection, lets ATS enter that async pause (so the handshake
eventfd is registered on the poller with the SSLNetVConnection as its target),
then closes the socket before the pause finishes. On that disconnect the
SSLNetVConnection must deregister the eventfd before it is freed, otherwise the
poller is left with a live registration pointing at a freed connection. Under
ASan a server that fails to deregister reports an error here; a correct server
stays clean.
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
