#!/usr/bin/env python3
'''
Drive an OpenSSL s_client connection that completes a TLSv1.2 handshake and
then requests a client-initiated renegotiation, with ATS configured to allow
client renegotiation. Verifies that ATS answers the renegotiation attempt
promptly -- either by completing it (libraries that honor it) or by
delivering its refusal alert (OpenSSL 3.x never honors a client
renegotiation unless SSL_OP_ALLOW_CLIENT_RENEGOTIATION is set) -- instead of
stranding the TLS response in its write buffer and leaving the client
hanging.
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
import threading
import time


class OutputCollector:
    '''Accumulate a subprocess's merged output from a reader thread.'''

    def __init__(self, stream):
        self._stream = stream
        self._lock = threading.Lock()
        self._data = b''
        self._thread = threading.Thread(target=self._read, daemon=True)
        self._thread.start()

    def _read(self):
        while True:
            chunk = self._stream.read1(4096)
            if not chunk:
                break
            with self._lock:
                self._data += chunk

    def data(self) -> bytes:
        with self._lock:
            return self._data


def wait_for(predicate, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.1)
    return predicate()


def main() -> int:
    parser = argparse.ArgumentParser(description='Trigger a TLSv1.2 client renegotiation against ATS and require a prompt answer.')
    parser.add_argument('-p', '--ats-port', type=int, dest='ats_port', required=True, help='ATS TLS port number')
    parser.add_argument('-s', '--server-name', type=str, dest='sni', default='example.com', help='SNI server name')
    args = parser.parse_args()

    # -tls1_2 forces a protocol that supports renegotiation (TLS 1.3 has none).
    # No -quiet: it suppresses s_client's interpretation of the "R" command, so
    # the renegotiation request would otherwise be sent as plain application data.
    cmd = shlex.split(
        f'openssl s_client -connect 127.0.0.1:{args.ats_port} -tls1_2 -servername {args.sni} -cipher DEFAULT@SECLEVEL=0')
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = OutputCollector(proc.stdout)
    verdict = 'RENEGOTIATION-STALLED'

    try:
        request = b'GET / HTTP/1.1\r\nHost: ' + args.sni.encode() + b'\r\n\r\n'
        proc.stdin.write(request)
        proc.stdin.flush()
        if not wait_for(lambda: b'HTTP/1.1 200 OK' in output.data(), 10):
            print('NO-FIRST-RESPONSE: the request before the renegotiation never completed')
            proc.kill()
            return 1

        # Let the first response fully drain so the proxy's TLS write face goes
        # idle: that is exactly when the renegotiation answer has no in-flight
        # transport write to ride out on, so a layered proxy that fails to flush
        # SSL protocol output on its own strands the answer and the client hangs.
        time.sleep(2)

        # s_client interprets a line containing just "R" as a request to
        # renegotiate the session.
        proc.stdin.write(b'R\n')
        proc.stdin.flush()
        time.sleep(1)
        # A second request provokes the client into resolving the
        # renegotiation it started: it either completes it and sends the
        # request, or it reads the server's refusal alert and exits.
        try:
            proc.stdin.write(request)
            proc.stdin.flush()
        except BrokenPipeError:
            # The client already exited on the refusal alert.
            pass

        def renegotiation_answered():
            return output.data().count(b'HTTP/1.1 200 OK') >= 2 or proc.poll() is not None

        if wait_for(renegotiation_answered, 10):
            if output.data().count(b'HTTP/1.1 200 OK') >= 2:
                verdict = 'RENEGOTIATION-COMPLETED'
            else:
                verdict = 'RENEGOTIATION-REFUSED-PROMPTLY'
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.wait()

    sys.stdout.write(output.data().decode('utf-8', errors='replace'))
    print(verdict)
    return 0 if verdict != 'RENEGOTIATION-STALLED' else 1


if __name__ == '__main__':
    exit(main())
