#!/usr/bin/env python3
'''
Drive an OpenSSL s_client connection that completes a TLSv1.2 handshake and
then requests a client-initiated renegotiation. Used to verify that Traffic
Server refuses the renegotiation without crashing.
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
import shlex
import subprocess
import sys
import time


def main() -> None:
    parser = argparse.ArgumentParser(description='Trigger a TLSv1.2 client renegotiation against ATS.')
    parser.add_argument('-p', '--ats-port', type=int, dest='ats_port', required=True, help='ATS TLS port number')
    parser.add_argument('-s', '--server-name', type=str, dest='sni', default='example.com', help='SNI server name')
    args = parser.parse_args()

    # -tls1_2 forces a protocol that supports renegotiation (TLS 1.3 has none).
    # No -quiet: it suppresses s_client's interpretation of the "R" command, so
    # the renegotiation request would otherwise be sent as plain application data.
    cmd = shlex.split(
        f'openssl s_client -connect 127.0.0.1:{args.ats_port} -tls1_2 -servername {args.sni} -cipher DEFAULT@SECLEVEL=0')
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    # Complete the handshake and one request, then send "R" on its own line:
    # s_client interprets that as a request to renegotiate the session.
    try:
        proc.stdin.write(b'GET / HTTP/1.1\r\nHost: ' + args.sni.encode() + b'\r\n\r\n')
        proc.stdin.flush()
        time.sleep(2)
        proc.stdin.write(b'R\n')
        proc.stdin.flush()
        time.sleep(2)
        proc.stdin.write(b'GET / HTTP/1.1\r\nHost: ' + args.sni.encode() + b'\r\n\r\n')
        proc.stdin.flush()
        out, _ = proc.communicate(timeout=10)
    except (subprocess.TimeoutExpired, BrokenPipeError):
        proc.kill()
        out, _ = proc.communicate()

    sys.stdout.write(out.decode('utf-8', errors='replace'))
    # The client's own exit status is irrelevant: ATS legitimately tears down
    # the connection on the refused renegotiation. The test asserts on ATS
    # staying alive, not on this process.
    exit(0)


if __name__ == '__main__':
    main()
