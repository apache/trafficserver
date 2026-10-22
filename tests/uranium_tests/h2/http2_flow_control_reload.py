"""Probe HTTP/2 receive windows while reloading flow control policies."""

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
import concurrent.futures
import http.client
import socket
import ssl
import subprocess
import time

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import PingAckReceived, RequestReceived, StreamReset
from h2.settings import SettingCodes


class WindowProbe:
    """Measure ATS receive windows from each side of a fresh connection."""

    def __init__(self, args: argparse.Namespace) -> None:
        self._args = args
        self._client_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        self._client_context.check_hostname = False
        self._client_context.verify_mode = ssl.CERT_NONE
        self._client_context.set_alpn_protocols(['h2'])
        self._server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self._server_context.load_cert_chain(args.cert, args.key)
        self._server_context.set_alpn_protocols(['h2'])

    def _window(self, sock: ssl.SSLSocket, is_client: bool) -> int:
        assert sock.selected_alpn_protocol() == 'h2', 'Expected HTTP/2'
        connection = H2Connection(config=H2Configuration(client_side=is_client))
        connection.initiate_connection()
        connection.ping(b'reload!!')
        sock.sendall(connection.data_to_send())
        # The PING acknowledgement is a barrier: ATS has sent its initial
        # SETTINGS and connection WINDOW_UPDATE before acknowledging our PING.
        acknowledged = False
        received_request = is_client
        while not (acknowledged and received_request):
            data = sock.recv(65536)
            assert data, 'ATS closed the connection before the probe completed'
            for event in connection.receive_data(data):
                if isinstance(event, PingAckReceived):
                    acknowledged = True
                elif isinstance(event, RequestReceived):
                    received_request = True
                    connection.send_headers(event.stream_id, [(':status', '200'), ('content-length', '0')], end_stream=True)
            sock.sendall(connection.data_to_send())
        return connection.outbound_flow_control_window

    def inbound(self) -> int:
        with socket.create_connection(('127.0.0.1', self._args.https_port), timeout=5) as raw:
            with self._client_context.wrap_socket(raw, server_hostname='localhost') as sock:
                return self._window(sock, True)

    def _request(self) -> None:
        connection = http.client.HTTPConnection('127.0.0.1', self._args.http_port, timeout=5)
        try:
            connection.request('GET', '/', headers={'Connection': 'close'})
            response = connection.getresponse()
            assert response.status == 200, f'Unexpected origin response: {response.status}'
            response.read()
        finally:
            connection.close()

    def outbound(self) -> int:
        with socket.socket() as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(('127.0.0.1', self._args.origin_port))
            listener.listen(1)
            listener.settimeout(5)
            with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
                request = executor.submit(self._request)
                raw, _ = listener.accept()
                with raw:
                    raw.settimeout(5)
                    with self._server_context.wrap_socket(raw, server_side=True) as sock:
                        window = self._window(sock, False)
                        request.result(timeout=5)
                return window

    def expect(self, inbound_policy: int, outbound_policy: int, wait: bool = False) -> None:
        expected = tuple(65535 if policy == 0 else 6553500 for policy in (inbound_policy, outbound_policy))
        deadline = time.monotonic() + (20 if wait else 0)
        while True:
            actual = (self.inbound(), self.outbound())
            if actual == expected:
                print(f'policies in={inbound_policy}, out={outbound_policy}: windows={actual}', flush=True)
                return
            if time.monotonic() >= deadline:
                raise AssertionError(
                    f'policies in={inbound_policy}, out={outbound_policy}: expected windows {expected}, got {actual}')
            time.sleep(0.25)

    def _ping(self, sock: ssl.SSLSocket, connection: H2Connection) -> None:
        connection.ping(b'policy!!')
        sock.sendall(connection.data_to_send())
        while True:
            data = sock.recv(65536)
            assert data, 'ATS closed the persistent connection'
            events = connection.receive_data(data)
            assert not any(isinstance(event, StreamReset) for event in events), 'ATS reset a probe stream'
            sock.sendall(connection.data_to_send())
            if any(isinstance(event, PingAckReceived) for event in events):
                return

    def expect_retained_policy(self, original: int, updated: int) -> None:
        record = 'proxy.config.http2.flow_control.policy_in'
        subprocess.run(['traffic_ctl', 'config', 'set', record, str(original)], check=True, timeout=10)
        self.expect(original, 0, wait=True)
        # Hold origin connections in the listen queue so incomplete uploads stay
        # open while we inspect ATS's stream settings, without completing TLS.
        with socket.socket() as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(('127.0.0.1', self._args.origin_port))
            listener.listen(10)
            with socket.create_connection(('127.0.0.1', self._args.https_port), timeout=5) as raw:
                with self._client_context.wrap_socket(raw, server_hostname='localhost') as sock:
                    connection = H2Connection(config=H2Configuration(client_side=True))
                    connection.initiate_connection()
                    self._ping(sock, connection)
                    subprocess.run(['traffic_ctl', 'config', 'set', record, str(updated)], check=True, timeout=10)
                    deadline = time.monotonic() + 20
                    expected = 65535 if updated == 0 else 6553500
                    while self.inbound() != expected:
                        assert time.monotonic() < deadline, 'New connections did not pick up the updated policy'
                        time.sleep(0.25)
                    for stream_id in (1, 3):
                        connection.send_headers(
                            stream_id, [
                                (':method', 'POST'), (':scheme', 'https'), (':authority', 'localhost'), (':path', '/'),
                                ('content-length', '1')
                            ],
                            end_stream=False)
                        self._ping(sock, connection)
                    expected_stream_window = 65535 if original == 0 else 6553500 // 2
                    actual = connection.remote_settings[SettingCodes.INITIAL_WINDOW_SIZE]
                    assert actual == expected_stream_window, (
                        f'Existing connection changed policy {original} -> {updated}: '
                        f'expected stream window {expected_stream_window}, got {actual}')
        print(f'PASS: existing connection retained policy {original} after reload to {updated}', flush=True)

    def run(self) -> None:
        self.expect(0, 0)
        # Reset to policy 0 before policy 2 so the large window proves that
        # both nonzero policy values actually take effect. Probe the opposite
        # direction too, so crossed or shared callbacks cannot pass.
        for direction in ('in', 'out'):
            record = f'proxy.config.http2.flow_control.policy_{direction}'
            for policy in (1, 0, 2, 0):
                subprocess.run(['traffic_ctl', 'config', 'set', record, str(policy)], check=True, timeout=10)
                self.expect(policy if direction == 'in' else 0, policy if direction == 'out' else 0, wait=True)
        # Exercise the records.yaml reload path as well as live config set.
        for policy in (1, 0):
            for direction in ('in', 'out'):
                record = f'proxy.config.http2.flow_control.policy_{direction}'
                subprocess.run(['traffic_ctl', 'config', 'set', record, str(policy), '--cold'], check=True, timeout=10)
            subprocess.run(['traffic_ctl', 'config', 'reload', '--monitor'], check=True, timeout=30)
            self.expect(policy, policy, wait=True)
        self.expect_retained_policy(0, 2)
        self.expect_retained_policy(2, 0)
        print('PASS: both flow control policies reloaded', flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--https-port', type=int, required=True)
    parser.add_argument('--http-port', type=int, required=True)
    parser.add_argument('--origin-port', type=int, required=True)
    parser.add_argument('--cert', required=True)
    parser.add_argument('--key', required=True)
    WindowProbe(parser.parse_args()).run()


if __name__ == '__main__':
    main()
