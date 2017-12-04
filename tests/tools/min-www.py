# A very minimal web server. It responds to any request with the same response.
# If the request url path is '/stop' the server shuts down.

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
import threading
import argparse

PAGE_CONTENT = b"Grigor Grigor Grigor!\n"

class RequestHandler(http.server.BaseHTTPRequestHandler) :

    protocol_version = 'HTTP/1.1'

    def do_GET(self) :
        self.send_response(200, "OK")
        self.send_header("Content-Length", len(PAGE_CONTENT))
        if self.path == '/stop' :
            self.send_header('Note', 'Shutting down')
            threading.Thread(target=lambda : httpd.shutdown()).start()
        self.end_headers();
        self.wfile.write(PAGE_CONTENT)

    def do_HEAD(self) :
        self.send_response(200, "OK")
        self.end_headers();

    def do_DELETE(self) :
        self.send_response(200, "OK")
        self.end_headers();

command_parser = argparse.ArgumentParser()
command_parser.add_argument("port", help="Listen port", type=int, default=7000, nargs='?')
args = command_parser.parse_args()

print("Listening on {}".format(args.port))
httpd = http.server.HTTPServer(('', args.port), RequestHandler)
httpd.serve_forever()
