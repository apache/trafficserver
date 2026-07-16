#!/usr/bin/env python3
'''
A TLS origin that accepts one connection, reads the request headers, then
stops reading and aborts the connection with a TCP RST while the proxy is
still sending the request body.
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
import socket
import ssl
import struct
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description='TLS origin that RSTs mid-request-body.')
    parser.add_argument('-p', '--port', type=int, required=True, help='port to listen on')
    parser.add_argument('-c', '--cert', required=True, help='PEM file with certificate and key')
    parser.add_argument('-d', '--delay', type=float, default=1.0, help='seconds to wait after the headers before the RST')
    args = parser.parse_args()

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(args.cert)

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    # Keep the receive window small so the proxy cannot park the whole request
    # body in kernel buffers: it must still be mid-send when the RST arrives.
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
    listener.bind(('127.0.0.1', args.port))
    listener.listen(5)
    print(f'listening on {args.port}', flush=True)

    # Accept until a connection completes the TLS handshake and delivers the
    # request headers: the test harness probes the port (a bare TCP connect
    # then close) to detect readiness, and that probe must not consume the
    # one real connection.
    tls = None
    while tls is None:
        conn, addr = listener.accept()
        print(f'accepted connection from {addr}', flush=True)
        try:
            tls = context.wrap_socket(conn, server_side=True)
        except (ssl.SSLError, OSError) as e:
            print(f'TLS handshake failed (readiness probe?): {e}', flush=True)
            conn.close()
            tls = None
            continue
        print('TLS handshake complete', flush=True)
        data = b''
        while b'\r\n\r\n' not in data:
            chunk = tls.recv(4096)
            if not chunk:
                break
            data += chunk
        if b'\r\n\r\n' not in data:
            print('peer closed before request headers arrived', flush=True)
            tls.close()
            tls = None
            continue
    print('request headers received; not reading the body', flush=True)

    # Let the proxy get deep into sending the request body, then abort. With
    # SO_LINGER(0) the close discards buffered data and sends an RST; the
    # unread body bytes in the receive queue force an RST as well. Do not
    # unwrap()/shutdown the TLS layer: this is an abortive transport-level
    # close, not a clean close_notify.
    time.sleep(args.delay)
    tls.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
    tls.close()
    print('connection reset sent', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
