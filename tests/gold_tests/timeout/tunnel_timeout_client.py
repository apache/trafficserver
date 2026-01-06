#!/usr/bin/env python3
'''
A simple client that establishes a CONNECT tunnel and then holds the connection
idle to trigger an active timeout.
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

import socket
import sys
import time


def main():
    if len(sys.argv) < 6:
        print(f"Usage: {sys.argv[0]} proxy_host proxy_port target_host target_port sleep_seconds")
        sys.exit(1)

    proxy_host = sys.argv[1]
    proxy_port = int(sys.argv[2])
    target_host = sys.argv[3]
    target_port = int(sys.argv[4])
    sleep_seconds = int(sys.argv[5])

    # Connect to the proxy
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10)

    try:
        sock.connect((proxy_host, proxy_port))
        print(f"Connected to proxy {proxy_host}:{proxy_port}")

        # Send CONNECT request
        connect_request = f"CONNECT {target_host}:{target_port} HTTP/1.1\r\nHost: {target_host}:{target_port}\r\n\r\n"
        sock.sendall(connect_request.encode())
        print(f"Sent CONNECT request for {target_host}:{target_port}")

        # Read the response
        response = b""
        while b"\r\n\r\n" not in response:
            data = sock.recv(1024)
            if not data:
                break
            response += data

        response_str = response.decode()
        print(f"Received response: {response_str.strip()}")

        if "200" not in response_str:
            print(f"CONNECT failed: {response_str}")
            sock.close()
            sys.exit(1)

        print(f"Tunnel established, sleeping for {sleep_seconds} seconds to trigger active timeout...")

        # Now hold the connection idle until the active timeout fires
        # Use a longer socket timeout so we can detect when ATS closes the connection
        sock.settimeout(sleep_seconds + 5)

        try:
            # Wait for data or connection close
            data = sock.recv(1024)
            if not data:
                print("Connection closed by server (timeout)")
            else:
                print(f"Received data: {data}")
        except socket.timeout:
            print("Socket timeout waiting for server")
        except Exception as e:
            print(f"Exception: {e}")

    except socket.timeout:
        print("Socket timeout during connect/handshake")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        sock.close()

    print("Done")


if __name__ == "__main__":
    main()
