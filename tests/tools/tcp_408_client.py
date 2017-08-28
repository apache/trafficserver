'''
A simple command line interface to send/receive bytes over TCP.
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
import sys
import time


def tcp_client(host, port, sleep, header, data):
    pass
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    s.sendall(header.encode())
    s.sendall(data[0].encode())
    time.sleep(sleep)
    s.shutdown(socket.SHUT_WR)
    while True:
        output = s.recv(4096)  # suggested bufsize from docs.python.org
        if len(output) <= 0:
            break
        else:
            sys.stdout.write(output.decode())
    s.close()


DESCRIPTION =\
    """A simple command line interface to send/receive bytes over TCP.

The full contents of the given file are sent via a TCP connection to the given
host and port. Then data is read from the connection and printed to standard
output. Streaming is not supported."""


def main(argv):
    parser = argparse.ArgumentParser(description=DESCRIPTION)
    parser.add_argument('host', help='the target host')
    parser.add_argument('port', type=int, help='the target port')
    parser.add_argument('sleep', type=int, help='timeout')
    args = parser.parse_args()

    header = 'POST / HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 2\r\n\r\n'
    data = "aa"
    tcp_client(args.host, args.port, args.sleep, header, data)


if __name__ == "__main__":
    main(sys.argv)
