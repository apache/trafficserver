'''
An origin that closes the first connection after reading the request, without
responding, and answers every later request with a 200.
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
"""Origin for the QUERY revalidation test.

The first response to a request has an ETag and a short lifetime. A request
with a matching If-None-Match gets a 304 response.

The path selects a variant:

- /minimal: the 304 response has no freshness information.
- /noetag: the 200 response has no ETag.
"""

import http.server
import sys

ETAG = '"v1"'
BODY = b'answer'


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def do_QUERY(self) -> None:
        length = int(self.headers.get('Content-Length', 0))
        self.rfile.read(length)
        validator = self.headers.get('If-None-Match')
        conditional = validator == ETAG
        print(f'request method={self.command} path={self.path} conditional={conditional} validator={validator}', flush=True)
        if conditional or (self.path == '/noetag' and validator is not None):
            self.send_response(304)
            self.send_header('ETag', ETAG)
            if self.path != '/minimal':
                self.send_header('Cache-Control', 'max-age=300')
            self.end_headers()
            return
        self.send_response(200)
        if self.path != '/noetag':
            self.send_header('ETag', ETAG)
        self.send_header('Cache-Control', 'max-age=2' if self.path == '/minimal' else 'max-age=1')
        self.send_header('Content-Length', str(len(BODY)))
        self.end_headers()
        self.wfile.write(BODY)

    def log_message(self, format: str, *args: object) -> None:
        pass


def main() -> int:
    http.server.ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1])), Handler).serve_forever()
    return 0


if __name__ == '__main__':
    sys.exit(main())
