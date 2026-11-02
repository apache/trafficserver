#!/usr/bin/env python3
"""Send three POSTs that ATS multiplexes onto one HTTP/2 origin session."""

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

import http.client
import sys
import threading
import time

# Long enough for the first request to establish the origin session that the
# later ones then reuse.
LATER_REQUEST_DELAY_SECONDS = 1.0
LATER_PATHS = ("/goaway-split-high-1", "/goaway-split-high-2")


def post(port: int, path: str, results: dict[str, tuple[int, bytes]]) -> None:
    """POST the shared request body and record the response."""
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=15)
    connection.request("POST", path, body=b"request-body", headers={"Host": "safe-retry.example.com"})
    response = connection.getresponse()
    results[path] = (response.status, response.read())
    connection.close()


def main() -> int:
    """Expect the stream the GOAWAY covers to drain and the others to retry."""
    port = int(sys.argv[1])
    results: dict[str, tuple[int, bytes]] = {}
    threads = [threading.Thread(target=post, args=(port, "/goaway-split-low", results))]
    threads[0].start()
    time.sleep(LATER_REQUEST_DELAY_SECONDS)
    for path in LATER_PATHS:
        threads.append(threading.Thread(target=post, args=(port, path, results)))
        threads[-1].start()
        # Keep ATS's stream ids in request order.
        time.sleep(0.2)
    for thread in threads:
        thread.join()

    print(f"results={results}")
    assert results.get("/goaway-split-low") == (200, b"drained"), results
    for path in LATER_PATHS:
        assert results.get(path) == (200, b"retried"), results
    print("Covered stream drained and uncovered streams retried")
    return 0


if __name__ == "__main__":
    sys.exit(main())
