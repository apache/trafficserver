'''
A stand-in JSONRPC node for traffic_ctl tests. It answers every request with
the reply configured for its method, so a test can show traffic_ctl server
errors that no real handler produces. A method configured with a list of
replies gets them in turn, starting over after the last one.
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

import argparse
import json
import os
import socket

METHOD_NOT_FOUND = {'error': {'code': -32601, 'message': 'Method not found'}}


def read_request(conn: socket.socket) -> dict | None:
    data = b''
    while True:
        chunk = conn.recv(65536)
        if not chunk:
            return None
        data += chunk
        try:
            return json.loads(data)
        except json.JSONDecodeError:
            continue


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', help='runroot for traffic_ctl: runroot.yaml, the socket and a "ready" marker are created here')
    parser.add_argument('replies', help='JSON object mapping a method name to {"error": ...} or {"result": ...}, or a list of them')
    args = parser.parse_args()

    replies = json.loads(args.replies)
    calls = {}
    os.makedirs(args.directory, exist_ok=True)
    with open(os.path.join(args.directory, 'runroot.yaml'), 'w') as runroot:
        runroot.write('runtimedir: .\n')

    sock_path = os.path.join(args.directory, 'jsonrpc20.sock')
    if os.path.exists(sock_path):
        os.unlink(sock_path)

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
        server.bind(sock_path)
        server.listen()
        open(os.path.join(args.directory, 'ready'), 'w').close()
        while True:
            conn, _ = server.accept()
            with conn:
                request = read_request(conn)
                if request is None:
                    continue
                method = request.get('method')
                configured = replies.get(method, METHOD_NOT_FOUND)
                if isinstance(configured, list):
                    configured = configured[calls.get(method, 0) % len(configured)]
                    calls[method] = calls.get(method, 0) + 1
                reply = {'jsonrpc': '2.0', 'id': request.get('id')}
                reply.update(configured)
                conn.sendall(json.dumps(reply).encode())


if __name__ == '__main__':
    main()
