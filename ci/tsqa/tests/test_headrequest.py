'''
Test Head Request
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

import time
import logging
import SocketServer
import tsqa.test_cases
import helpers
import socket

log = logging.getLogger(__name__)


class HeadRequestServerHandler(SocketServer.BaseRequestHandler):
    """
    A subclass of RequestHandler which will response to head requests
    """

    def handle(self):
        # Receive the data in small chunks and retransmit it
        while True:
            data = self.request.recv(4096).strip()
            if data:
                log.debug('Sending data back to the client')
            else:
                log.debug('Client disconnected')
                break
            if 'TE' in data:
                resp = ('HTTP/1.1 200 OK\r\n'
                    'Server: Apache-Coyote/1.1\r\n'
                    'Transfer-Encoding: chunked\r\n'
                    'Vary: Accept-Encoding\r\n'
                    '\r\n'
                    )
                self.request.sendall(resp)
            elif 'CL' in data:
                resp = ('HTTP/1.1 200 OK\r\n'
                    'Server: Apache-Coyote/1.1\r\n'
                    'Content-Length: 123\r\n'
                    'Vary: Accept-Encoding\r\n'
                    '\r\n'
                    )
                self.request.sendall(resp)
            else:
                resp = ('HTTP/1.1 200 OK\r\n'
                    'Server: Apache-Coyote/1.1\r\n'
                    'Vary: Accept-Encoding\r\n'
                    '\r\n'
                    )
                self.request.sendall(resp)


class TestHeadRequestWithoutTimeout(helpers.EnvironmentCase):
    '''
    Tests for ATS handling head requests correctly without waiting for the http body
    '''
    @classmethod
    def setUpEnv(cls, env):
        cls.timeout = 5
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.transaction_no_activity_timeout_out': cls.timeout,
        })
        cls.socket_server = tsqa.endpoint.SocketServerDaemon(HeadRequestServerHandler)
        cls.socket_server.start()
        cls.socket_server.ready.wait()
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}/'.format(cls.socket_server.port))
        log.info('map / http://127.0.0.1:{0}/'.format(cls.socket_server.port))

        cls.proxy_host = '127.0.0.1'
        cls.proxy_port = int(cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])

    def test_head_request_without_timout(cls):
        request_cases = ['TE', 'CL', '']
        for request_case in request_cases:
            begin_time = time.time()
            conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            conn.connect((cls.proxy_host, cls.proxy_port))
            request_content = 'HEAD / HTTP/1.1\r\nConnection: close\r\nHost: 127.0.0.1\r\nContent-Length: %d\r\n\r\n%s' % (
                    len(request_case), request_case)
            conn.setblocking(1)
            conn.send(request_content)
            while 1:
                try:
                    resp = conn.recv(4096)
                    if len(resp) == 0:
                        break
                    response_content = resp
                    log.info(resp)
                except:
                    break
            conn.shutdown(socket.SHUT_RDWR)
            conn.close()
            end_time = time.time()
            log.info("head request with case(%s) costs %f seconds while the timout is %f seconds." % (
                    request_case, end_time - begin_time, cls.timeout))
            cls.assertGreater(cls.timeout, end_time - begin_time)
            if request_case == 'CL':
                cls.assertIn('Content-Length', response_content)
