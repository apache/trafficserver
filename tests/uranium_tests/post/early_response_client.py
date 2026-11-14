#!/usr/bin/env python3
'''Drive a POST followed by an ordinary request, to probe origin connection reuse.

Raw sockets rather than curl so that a declared request body can be left unsent,
and so no proxy environment variables are consulted.

In the default mode the POST declares a body and sends none of it, which leaves
the origin owed those bytes once it answers early.  With --complete-body the POST
instead waits for the 100 Continue and sends the whole body, which is what an
ordinary Expect: 100-continue POST does and must leave connection reuse intact.
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
import re
import socket
import time

WITHHELD_BODY_LENGTH = 100000
COMPLETED_BODY_LENGTH = 16


def read_response(conn: socket.socket) -> bytes:
    '''Read one response head.

    :param conn: Connection to read from.
    :return: The bytes read, empty on timeout or close.
    '''
    buf = b''
    while b'\r\n\r\n' not in buf:
        try:
            chunk = conn.recv(4096)
        except (OSError, socket.timeout):
            break
        if not chunk:
            break
        buf += chunk
    return buf


def status_of(response: bytes) -> str:
    '''Extract the status line of a response.

    :param response: The response bytes.
    :return: The status line, or NO-RESPONSE when nothing arrived.
    '''
    if not response:
        return 'NO-RESPONSE'
    return response.split(b'\r\n', 1)[0].decode(errors='replace')


def closes_connection(response: bytes) -> bool:
    '''Report whether a response asks to close the client connection.

    :param response: The response bytes.
    :return: True when Connection: close is present.
    '''
    return re.search(rb'\r\nconnection:\s*close', response, re.IGNORECASE) is not None


class HeadReader:
    """Read response heads from a connection without losing pipelined bytes.

    An origin may send several interim responses back to back, and ATS forwards them the
    same way, so more than one head can arrive in a single read.  Reading straight into a
    fresh buffer each time would discard the extra heads and then block forever waiting for
    a response that already arrived.
    """

    def __init__(self, conn: socket.socket) -> None:
        """Wrap a connection.

        :param conn: Connection to read from.
        """
        self._conn = conn
        self._buffer = b''

    def next_head(self) -> bytes:
        """Return the next response head, reading more only when needed.

        :return: The head including its terminating blank line, empty on timeout or close.
        """
        while b'\r\n\r\n' not in self._buffer:
            try:
                chunk = self._conn.recv(4096)
            except (OSError, socket.timeout):
                return b''
            if not chunk:
                return b''
            self._buffer += chunk

        head, _, rest = self._buffer.partition(b'\r\n\r\n')
        self._buffer = rest
        return head + b'\r\n\r\n'


def read_final_response(reader: HeadReader, label: str) -> bytes:
    """Read responses until a final one arrives, reporting each interim response.

    An origin may send more than one interim response before the final one, for instance
    103 Early Hints followed by 100 Continue, and ATS forwards each.  Stopping at the first
    response would leave the final one unread and close the connection mid transaction.

    :param reader: Head reader wrapping the connection.
    :param label: Prefix used in the log lines.
    :return: The final response bytes, empty if none arrived.
    """
    while True:
        response = reader.next_head()
        if not response:
            return b''
        status = status_of(response)
        if re.match(r'HTTP/1\.[01] 1\d\d', status):
            print(f'{label} INTERIM {status}', flush=True)
            continue
        return response


def send_withheld_post(port: int, path: str, expect_100_continue: bool, read_timeout: float) -> None:
    '''Send a POST that declares a body and sends none of it.

    :param port: Proxy port to connect to.
    :param path: Request target, which selects the origin's behavior.
    :param expect_100_continue: Whether to ask for a 100 Continue.
    :param read_timeout: Seconds to wait for a response.  Short where the response is
        expected to be withheld, so the scenario does not pay a full timeout for nothing.
    '''
    expect_header = b'Expect: 100-continue\r\n' if expect_100_continue else b''
    conn = socket.create_connection(('127.0.0.1', port), timeout=read_timeout)
    conn.sendall(
        f'POST {path} HTTP/1.1\r\n'.encode() + expect_header + b'Host: example.com\r\n'
        b'Content-Length: ' + str(WITHHELD_BODY_LENGTH).encode() + b'\r\n'
        b'\r\n'
    )
    response = read_response(conn)
    print(f'EARLY-POST STATUS {status_of(response)}', flush=True)
    # The client declared a body it never finished sending, so this connection is framed
    # ambiguously for a following request and ATS must not offer to keep it alive.  Report
    # it so the client half of the decision is observable and not merely assumed.
    print(f'EARLY-POST CLIENT-CLOSE {closes_connection(response)}', flush=True)
    conn.close()


def send_completed_post(port: int, path: str) -> None:
    '''Send an ordinary Expect: 100-continue POST and complete its body.

    :param port: Proxy port to connect to.
    :param path: Request target, which selects the origin's behavior.
    '''
    conn = socket.create_connection(('127.0.0.1', port), timeout=10)
    conn.sendall(
        f'POST {path} HTTP/1.1\r\n'.encode() + b'Expect: 100-continue\r\n'
        b'Host: example.com\r\n'
        b'Content-Length: ' + str(COMPLETED_BODY_LENGTH).encode() + b'\r\n'
        b'\r\n'
    )

    # Wait for the go-ahead, which may be preceded by other interim responses.
    reader = HeadReader(conn)
    while True:
        response = reader.next_head()
        if not response:
            print('COMPLETE-POST INTERIM NO-RESPONSE', flush=True)
            conn.close()
            return
        status = status_of(response)
        print(f'COMPLETE-POST INTERIM {status}', flush=True)
        if re.match(r'HTTP/1\.[01] 100', status):
            break

    conn.sendall(b'x' * COMPLETED_BODY_LENGTH)
    print(f'COMPLETE-POST STATUS {status_of(read_final_response(reader, "COMPLETE-POST"))}', flush=True)
    conn.close()


def main() -> int:
    '''Drive the POST and the following probe.

    :return: Process exit status.
    '''
    parser = argparse.ArgumentParser()
    parser.add_argument('port', type=int, help='Proxy port to connect to.')
    parser.add_argument(
        '--expect-100-continue', action='store_true', help='Ask for a 100 Continue so the body tunnel is never opened.'
    )
    parser.add_argument('--complete-body', action='store_true', help='Complete the declared body instead of withholding it.')
    parser.add_argument('--path', default='/early', help="Request target, which selects the origin's behavior.")
    parser.add_argument('--read-timeout', type=float, default=10.0, help='Seconds to wait for the first response.')
    args = parser.parse_args()

    if args.complete_body:
        send_completed_post(args.port, args.path)
    else:
        send_withheld_post(args.port, args.path, args.expect_100_continue, args.read_timeout)

    # Give ATS time to finish the transaction and decide what to do with the
    # origin connection.
    time.sleep(2)

    # A separate, ordinary request on its own client connection.  It must reach the
    # origin as a request.
    probe = socket.create_connection(('127.0.0.1', args.port), timeout=10)
    probe.sendall(b'GET /probe HTTP/1.1\r\nHost: example.com\r\n\r\n')
    print(f'PROBE STATUS {status_of(read_response(probe))}', flush=True)
    probe.close()

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
