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
"""Drive frame ordering that a complete-request replay cannot express."""
import http.client
from pathlib import Path
import time
import socket
import ssl
import sys

from hyperframe.frame import HeadersFrame, ContinuationFrame
from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import DataReceived, ResponseReceived, StreamEnded, StreamReset, ConnectionTerminated, TrailersReceived


def h2_request(port: int, path: str, split_request: bool = False) -> None:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    context.set_alpn_protocols(["h2"])
    h2 = H2Connection(config=H2Configuration(client_side=True, header_encoding="utf-8"))
    headers = [(":method", "GET"), (":scheme", "https"), (":authority", "cont.h2o.example.com"), (":path", path)]
    if path == "/304-data":
        headers.append(("if-none-match", '"old"'))
    with socket.create_connection(("127.0.0.1", port), timeout=10) as plain:
        with context.wrap_socket(plain, server_hostname="cont.h2o.example.com") as tls:
            h2.initiate_connection()
            tls.sendall(h2.data_to_send())
            h2.send_headers(1, headers, end_stream=True)
            wire = h2.data_to_send()
            if split_request:
                assert wire[3] == 1 and len(wire) == 9 + int.from_bytes(wire[:3], "big")
                head = HeadersFrame(1)
                head.flags.add("END_STREAM")
                head.data = wire[9:10]
                continuation = ContinuationFrame(1)
                continuation.flags.add("END_HEADERS")
                continuation.data = wire[10:]
                wire = head.serialize() + continuation.serialize()
            tls.sendall(wire)
            body = bytearray()
            status = None
            trailers = {}
            while True:
                data = tls.recv(65535)
                assert data, "ATS closed before END_STREAM"
                for event in h2.receive_data(data):
                    assert not isinstance(event, (StreamReset, ConnectionTerminated)), event
                    if isinstance(event, ResponseReceived):
                        status = dict(event.headers)[":status"]
                    elif isinstance(event, DataReceived):
                        body.extend(event.data)
                        h2.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                    elif isinstance(event, TrailersReceived):
                        trailers.update(event.headers)
                    elif isinstance(event, StreamEnded):
                        assert status == ("304" if path == "/304-data" else "200"), status
                        assert body == (b"" if path == "/304-data" else b"okay"), body
                        if path == "/trailers":
                            assert trailers["x-trailer-one"] == "one"
                            assert trailers["x-trailer-two"] == "two"
                        print(f"verified {path}")
                        return
                tls.sendall(h2.data_to_send())


def main() -> None:
    mode = sys.argv[3]
    if mode != "early":
        h2_request(int(sys.argv[2]), f"/{mode}", split_request=mode == "slow")
        return
    # Withhold the final request byte while the early response arrives:
    # the outbound stream MUST still be OPEN while ATS decodes CONTINUATION.
    client = http.client.HTTPConnection("127.0.0.1", int(sys.argv[1]), timeout=10)
    client.putrequest("POST", "/early")
    client.putheader("Content-Length", "2")
    client.endheaders(b"a")
    # A PING ACK after the response frames proves ATS processed CONTINUATION
    # before the final upload byte, even when early responses are deferred.
    ready = Path(sys.argv[4]).with_suffix(".early")
    deadline = time.monotonic() + 10
    while not ready.exists():
        assert time.monotonic() < deadline, "ATS did not acknowledge the early response frames"
        time.sleep(0.02)
    client.send(b"b")
    response = client.getresponse()
    assert response.status == 200
    assert response.getheader("x-continuation-padding-one")[9:] == "0123456789abcdef" * 1024
    assert response.read() == b"okay"
    client.close()
    print("verified OPEN continuation")


if __name__ == "__main__":
    main()
