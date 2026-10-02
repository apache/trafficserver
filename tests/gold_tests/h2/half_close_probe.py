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
"""Keep a locally half-closed origin session alive during subsequent lookups."""
import concurrent.futures
import http.client
from pathlib import Path
import socket
import re
import ssl
import sys
import threading
import time

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import RequestReceived, ConnectionTerminated

from origin_lifecycle import OriginLifecycle, send_bytes


def origin(port: int, cert: str, key: str, stop: str) -> None:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    context.set_alpn_protocols(["h2"])
    lifecycle = OriginLifecycle(stop)
    counts = [0, 0]
    lock = threading.Lock()

    def serve(plain: socket.socket) -> None:
        try:
            tls = context.wrap_socket(plain, server_side=True)
        except (ssl.SSLError, TimeoutError):
            plain.close()
            return
        with tls:
            assert tls.selected_alpn_protocol() == "h2"
            with lock:
                counts[0] += 1
                number = counts[0]
            conn = H2Connection(config=H2Configuration(client_side=False, header_encoding="utf-8"))
            conn.initiate_connection()
            send_bytes(tls, conn.data_to_send())
            tls.settimeout(0.2)
            while not lifecycle.stopped:
                try:
                    data = tls.recv(65535)
                except TimeoutError:
                    continue
                if not data:
                    return
                for event in conn.receive_data(data):
                    if isinstance(event, RequestReceived):
                        with lock:
                            counts[1] += 1
                        conn.send_headers(event.stream_id, [(":status", "200"), ("content-length", "2")])
                        conn.send_data(event.stream_id, b"ok" if number != 1 else b"o", end_stream=number != 1)
                        send_bytes(tls, conn.data_to_send())
                        if number == 1:
                            # DATA on stream zero fails frame-header validation,
                            # causing ATS to send GOAWAY and half-close locally.
                            send_bytes(tls, bytes.fromhex("000000000000000000"))
                    elif isinstance(event, ConnectionTerminated):
                        assert number == 1 and event.error_code == 1, event
                        Path(f"{stop}.ready").touch()
                        while not lifecycle.stopped:
                            time.sleep(0.02)
                        return
                wire = conn.data_to_send()
                if wire:
                    send_bytes(tls, wire)

    futures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        with socket.create_server(("127.0.0.1", port)) as listener:
            listener.settimeout(0.2)
            while not lifecycle.stopped:
                for future in futures:
                    if future.done():
                        future.result()
                try:
                    plain, _ = listener.accept()
                except TimeoutError:
                    continue
                plain.settimeout(5)
                futures.append(pool.submit(serve, plain))
        for future in futures:
            future.result()
    assert counts == [2, 3], f"Expected two sessions and three requests, got {counts}"
    print("half_close sessions=2 requests=3", flush=True)
    lifecycle.complete()


def client(port: int, stop: str, traffic_out: str) -> None:
    first = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    first.request("GET", "/held")
    response = first.getresponse()
    assert response.status == 200 and response.read(1) == b"o"
    deadline = time.monotonic() + 5
    while not Path(f"{stop}.ready").exists():
        assert time.monotonic() < deadline, "Origin did not receive ATS GOAWAY"
        time.sleep(0.02)
    for path in ("/while-held", "/after-release"):
        follow = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        follow.request("GET", path)
        result = follow.getresponse()
        assert result.status == 200 and result.read() == b"ok"
        follow.close()
        if path == "/while-held":
            try:
                response.read()
                raise AssertionError("The intentionally incomplete response must fail")
            except http.client.IncompleteRead as error:
                assert error.partial == b""
            response.close()
            first.close()
    log = Path(traffic_out).read_text()
    closed = re.search(r"\[(\d+)\] session half-close local", log)
    assert closed, "Must exercise local half-close"
    session = closed.group(1)
    later = log[closed.end():]
    assert re.search(rf"\[{session}\] \[\d+\] Delete stream", later), "Must release the held stream"
    assert not re.search(rf"\[{session}\] \[add session\]", later), "Half-closed session reentered the pool"
    print("half_close follow-on requests succeeded")


if __name__ == "__main__":
    if sys.argv[1] == "origin":
        origin(int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5])
    else:
        client(int(sys.argv[2]), sys.argv[3], sys.argv[4])
