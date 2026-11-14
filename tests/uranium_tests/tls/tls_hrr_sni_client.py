#!/usr/bin/env python3
"""Send one HTTPS request over TLS 1.3, optionally naming a different server after a HelloRetryRequest.

The server is expected to accept only a group the client offers no key share
for, so that it answers the first ClientHello with a HelloRetryRequest. The first ClientHello
carries --sni1 and the second carries --sni2. The script prints whether a
HelloRetryRequest happened, whether the server asked for a client certificate,
how the handshake ended, and the response status line.
"""
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

from OpenSSL import SSL

# OpenSSL state names reported to the info callback.
WRITE_CLIENT_HELLO = b"SSLv3/TLS write client hello"
READ_SERVER_HELLO = b"SSLv3/TLS read server hello"
READ_CERTIFICATE_REQUEST = b"SSLv3/TLS read server certificate request"


class HandshakeObserver:
    """Info callback that swaps the server name for the second ClientHello and records what the server sent."""

    def __init__(self, second_name: bytes) -> None:
        """
        :param second_name: Server name to send in the ClientHello that follows a HelloRetryRequest.
        """

        self._second_name = second_name
        self._previous_state = b""
        self._swapped = False
        self.client_hellos = 0
        self.certificate_request = False

    def __call__(self, connection: SSL.Connection, where: int, _ret: int) -> None:
        """Track the handshake state that @a connection is leaving, as reported for each transition.

        :param connection: The client connection.
        :param where: Which kind of event the TLS library is reporting.
        :param _ret: Unused event detail.
        """

        if not where & SSL.SSL_CB_LOOP:
            return
        state = connection.get_state_string()
        if state == WRITE_CLIENT_HELLO and self._previous_state != WRITE_CLIENT_HELLO:
            self.client_hellos += 1
        # The first server hello is the HelloRetryRequest when one is sent. The client builds its
        # second ClientHello after this point, so that is where the new name has to be set.
        if state == READ_SERVER_HELLO and not self._swapped:
            connection.set_tlsext_host_name(self._second_name)
            self._swapped = True
        if state == READ_CERTIFICATE_REQUEST:
            self.certificate_request = True
        self._previous_state = state


def parse_args() -> argparse.Namespace:
    """Return the command-line options."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True, help="Traffic Server TLS port on 127.0.0.1.")
    parser.add_argument("--sni1", required=True, help="Server name in the first ClientHello.")
    parser.add_argument("--sni2", required=True, help="Server name in the ClientHello after the HelloRetryRequest.")
    parser.add_argument("--host", required=True, help="Host header of the request.")
    parser.add_argument("--cert", help="Client certificate to offer.")
    parser.add_argument("--key", help="Private key of --cert.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    context = SSL.Context(SSL.TLS_METHOD)
    context.set_min_proto_version(SSL.TLS1_3_VERSION)
    context.set_verify(SSL.VERIFY_NONE)
    if args.cert:
        context.use_certificate_file(args.cert)
        context.use_privatekey_file(args.key)

    observer = HandshakeObserver(args.sni2.encode())
    sock = socket.create_connection(("127.0.0.1", args.port), timeout=10)
    # pyOpenSSL reports a socket with a timeout as WantReadError instead of waiting on it. The test
    # bounds the run with its own process timeout.
    sock.settimeout(None)
    connection = SSL.Connection(context, sock)
    connection.set_connect_state()
    connection.set_tlsext_host_name(args.sni1.encode())
    connection.set_info_callback(observer)

    try:
        connection.do_handshake()
        handshake = "ok"
    except SSL.Error as error:
        handshake = f"failed {error!r}"

    status = "none"
    if handshake == "ok":
        try:
            connection.sendall(f"GET / HTTP/1.1\r\nHost: {args.host}\r\nConnection: close\r\n\r\n".encode())
            response = b""
            while b"\r\n" not in response:
                chunk = connection.recv(4096)
                if not chunk:
                    break
                response += chunk
            status = response.split(b"\r\n", 1)[0].decode(errors="replace") if response else "closed"
        except (SSL.Error, OSError) as error:
            status = f"error {error!r}"
    sock.close()

    print(f"hrr={observer.client_hellos == 2}")
    print(f"certificate_request={observer.certificate_request}")
    print(f"handshake={handshake}")
    print(f"status={status}")


if __name__ == "__main__":
    main()
