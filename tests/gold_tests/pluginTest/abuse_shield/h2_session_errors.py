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
"""Generate HTTP/2 errors independently of transaction completion."""

import argparse
import concurrent.futures
import socket
import ssl
import threading

from hpack import Encoder
from hyperframe.frame import DataFrame, Frame, GoAwayFrame, HeadersFrame, SettingsFrame


def receive_exact(connection: ssl.SSLSocket, size: int) -> bytes:
    data = b""
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise EOFError("Connection closed before the expected frame")
        data += chunk
    return data


def receive_frame(connection: ssl.SSLSocket) -> Frame:
    frame, length = Frame.parse_frame_header(memoryview(receive_exact(connection, 9)))
    frame.parse_body(memoryview(receive_exact(connection, length)))
    return frame


def send_error(port: int, mode: str, barrier: threading.Barrier) -> None:
    context = ssl._create_unverified_context()
    context.set_alpn_protocols(["h2"])
    with socket.create_connection(("127.0.0.1", port), timeout=5) as tcp:
        with context.wrap_socket(tcp, server_hostname="localhost") as connection:
            connection.settimeout(5)
            connection.sendall(b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n" + SettingsFrame(0).serialize())
            while True:
                frame = receive_frame(connection)
                if isinstance(frame, SettingsFrame) and "ACK" not in frame.flags:
                    connection.sendall(SettingsFrame(0, flags=["ACK"]).serialize())
                    break
            barrier.wait(timeout=10)
            if mode == "limit":
                encoder = Encoder()
                frames = []
                for stream_id in (1, 3, 5):
                    headers = HeadersFrame(stream_id, flags=["END_HEADERS"])
                    headers.data = encoder.encode(
                        [
                            (":method", "POST"), (":scheme", "https"), (":authority", "localhost"), (":path", "/"),
                            ("content-length", "10000")
                        ])
                    frames.append(headers.serialize())
                connection.sendall(b"".join(frames))
                expected_code = 9
            elif mode in ("received", "normal"):
                connection.sendall(GoAwayFrame(0, error_code=2 if mode == "received" else 0).serialize())
                return
            else:
                # DATA on stream zero is a connection error before any HEADERS.
                data = DataFrame(1, data=b"invalid")
                data.stream_id = 0
                connection.sendall(data.serialize())
                expected_code = 1
            while True:
                frame = receive_frame(connection)
                if isinstance(frame, GoAwayFrame):
                    assert frame.error_code == expected_code, (frame.error_code, expected_code)
                    return


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--mode", choices=["invalid", "limit", "received", "normal", "block"], required=True)
    args = parser.parse_args()
    barrier = threading.Barrier(8)
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        futures = [executor.submit(send_error, args.port, args.mode, barrier) for _ in range(8)]
        for future in futures:
            future.result()
    print("Completed eight HTTP/2 sessions")


if __name__ == "__main__":
    main()
