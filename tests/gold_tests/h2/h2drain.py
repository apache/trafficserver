'''
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


import json
import argparse
import hyper
import unittest
import time
import os
import twisted.internet._sslverify as v
from twisted.internet import reactor
from twisted.internet.endpoints import connectProtocol, SSL4ClientEndpoint
from twisted.internet.protocol import Protocol
from twisted.internet.ssl import optionsForClientTLS
from hyperframe.frame import SettingsFrame
from h2.connection import H2Connection
from h2.events import (
    ResponseReceived, DataReceived, StreamEnded,
    StreamReset, SettingsAcknowledged, ConnectionTerminated
)


# The MIT License (MIT)
#
# Copyright (c) 2014 Cory Benfield, Google Inc
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

class H2Protocol(Protocol):
    def __init__(self, authority, path, before_send_request_cb=lambda: None):
        self._conn = H2Connection()
        self._known_proto = None
        self._request_made = False
        self._authority = authority
        self._path = path
        self._goaway_received = False
        self._before_send_request_cb = before_send_request_cb

    def connectionMade(self):
        self._conn.initiate_connection()
        self._conn.update_settings({SettingsFrame.HEADER_TABLE_SIZE: 4096})
        self.transport.write(self._conn.data_to_send())

    def dataReceived(self, data):
        if not self._known_proto:
            self._known_proto = self.transport.negotiatedProtocol
            assert self._known_proto == b'h2'

        events = self._conn.receive_data(data)

        for event in events:
            if isinstance(event, ResponseReceived):
                self.handleResponse(event.headers, event.stream_id)
            elif isinstance(event, DataReceived):
                self.handleData(event.data, event.stream_id)
            elif isinstance(event, StreamEnded):
                self.endStream(event.stream_id)
            elif isinstance(event, SettingsAcknowledged):
                self.settingsAcked(event)
            elif isinstance(event, StreamReset):
                reactor.stop()
                raise RuntimeError("Stream reset: %d" % event.error_code)
            elif isinstance(event, ConnectionTerminated):
                # The ConnectionTerminated event is fired when a connection is torn down by the remote peer using a GOAWAY frame.
                # Details can be found here. https://python-hyper.org/projects/h2/en/v3.0.0/api.html#h2.events.ConnectionTerminated
                self._goaway_received = True
                reactor.stop()
            else:
                print(event)

        data = self._conn.data_to_send()
        if data:
            self.transport.write(data)

    def settingsAcked(self, event):
        # Having received the remote settings change, lets send our request.
        if not self._request_made:
            self._before_send_request_cb()
            self.sendRequest()

    def handleResponse(self, response_headers, stream_id):
        pass

    def handleData(self, data, stream_id):
        pass

    def endStream(self, stream_id):
        self._conn.close_connection()
        self.transport.write(self._conn.data_to_send())
        self.transport.loseConnection()
        reactor.stop()

    def sendRequest(self):
        request_headers = [
            (':method', 'GET'),
            (':authority', self._authority),
            (':scheme', 'https'),
            (':path', self._path),
        ]
        self._conn.send_headers(1, request_headers, end_stream=False)
        self._request_made = True


class HTTP2Drain:
    '''
    Test HTTP/2 drain feature.
    With this feature, client connnect will get GOAWAY when stopping
        ATS instead of being dropped ruthlessly
    '''

    def __init__(self, port):
        self.host = '127.0.0.1'
        self.https_port = port

    def http2_drain(self):
        # bypassing the cert verification
        v.verifyHostname = lambda x, y: None
        v.platformTrust = lambda: None
        options = optionsForClientTLS(
            hostname=self.host,
            acceptableProtocols=[b'h2'],
        )

        path = '/'
        # send trafficserver a signal, then send the request
        h2DrainClient = H2Protocol(self.host, path, before_send_request_cb=lambda: [
                                   os.system("pkill -f traffic_server"), time.sleep(1)])
        connectProtocol(
            SSL4ClientEndpoint(reactor, self.host, self.https_port, options),
            h2DrainClient
        )
        reactor.run()
        if h2DrainClient._goaway_received:
            print('PASS')
        else:
            print('FAIL')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", "-p",
                        type=int,
                        help="Port to use")
    args = parser.parse_args()
    h2drain = HTTP2Drain(args.port)
    h2drain.http2_drain()


if __name__ == '__main__':
    main()
