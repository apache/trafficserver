#!/usr/bin/env python3
'''
Stateful origin server for slice content-shrink underflow test.

Serves different Content-Range responses based on request sequence to simulate
content shrinking between fetches.

Usage: python3 shrink_origin.py <port>

Request sequence for /shrink with blockbytes=7, client requesting bytes=14-20:
  1. GET /shrink Range: bytes=0-6  (reference block, RefType::First)
     Response: 206, Content-Range: bytes 0-6/21, Etag: "old", body: 7 bytes
  2. GET /shrink Range: bytes=14-20  (block 2, first client block)
     Response: 206, Content-Range: bytes 14-20/10, Etag: "new", body: 7 bytes
     -> triggers mismatch (etag differs), m_contentlen updated to 10
  3. GET /shrink Range: bytes=0-6  (reference refetch)
     Response: 206, Content-Range: bytes 0-6/10, Etag: "new", body: 7 bytes
     -> blockpos=14 > m_contentlen=10 => underflow guard triggers
'''

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

import http.server
import sys
import threading

# Track how many times each range has been requested
request_counts = {}
lock = threading.Lock()


class ShrinkHandler(http.server.BaseHTTPRequestHandler):

    def do_GET(self):
        if self.path == '/ruok':
            self.send_response(200)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return

        range_hdr = self.headers.get('Range', '')

        with lock:
            key = f"{self.path}|{range_hdr}"
            request_counts[key] = request_counts.get(key, 0) + 1
            count = request_counts[key]

        if self.path == '/shrink':
            self._handle_shrink(range_hdr, count)
        elif self.path == '/shrink_mid':
            self._handle_shrink_mid(range_hdr, count)
        else:
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.end_headers()

    def _handle_shrink(self, range_hdr, count):
        # Parse range: "bytes=START-END"
        body = b'x' * 7  # always 7 bytes body for blockbytes=7

        if range_hdr == 'bytes=0-6':
            if count <= 1:
                # First request for block 0 (reference): original content
                self._send_206('bytes 0-6/21', '"old"', body)
            else:
                # Second request for block 0 (reference refetch after mismatch):
                # Report shrunk content-length=10, new etag
                self._send_206('bytes 0-6/10', '"new"', body)
        elif range_hdr == 'bytes=14-20':
            # Block 2 (interior): shrunk content, different etag => mismatch
            self._send_206('bytes 14-20/10', '"new"', body)
        else:
            self.send_response(416)
            self.send_header('Content-Length', '0')
            self.end_headers()

    def _handle_shrink_mid(self, range_hdr, count):
        """Non-block-aligned range case.

        Client requests bytes=16-20, blockbytes=7.
        blockpos=14, but m_req_range.m_beg=16.
        Content shrinks to 15: above blockpos but below range start.
        """
        body = b'y' * 7

        if range_hdr == 'bytes=0-6':
            if count <= 1:
                # Reference: original size
                self._send_206('bytes 0-6/21', '"old"', body)
            else:
                # Reference refetch: shrunk to 15
                self._send_206('bytes 0-6/15', '"new"', body)
        elif range_hdr == 'bytes=14-20':
            # Block 2 (interior): shrunk content, different etag
            self._send_206('bytes 14-20/15', '"new"', body)
        else:
            self.send_response(416)
            self.send_header('Content-Length', '0')
            self.end_headers()

    def _send_206(self, content_range, etag, body):
        self.send_response(206)
        self.send_header('Content-Range', content_range)
        self.send_header('Etag', etag)
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Cache-Control', 'max-age=0')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        # Suppress default logging
        pass


if __name__ == '__main__':
    port = int(sys.argv[1])
    server = http.server.HTTPServer(('127.0.0.1', port), ShrinkHandler)
    print(f"Shrink origin listening on port {port}", flush=True)
    server.serve_forever()
