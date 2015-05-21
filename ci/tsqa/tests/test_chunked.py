'''
Test chunked request/responses
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

import requests
import time
import logging
import json
import uuid
import socket

import helpers

import tsqa.test_cases
import tsqa.utils
import tsqa.endpoint

log = logging.getLogger(__name__)

import SocketServer


class ChunkedHandler(SocketServer.BaseRequestHandler):
    """
    A subclass of RequestHandler which return chunked encoding optionally

    /parts/sleep_time/close
        parts: number of parts to send
        sleep_time: time between parts
        close: bool wether to close properly
    """

    def handle(self):
        # Receive the data in small chunks and retransmit it
        conn_id = uuid.uuid4().hex
        while True:
            data = self.request.recv(4096).strip()
            if data:
                log.info('sending data back to the client')
            else:
                log.info('Client disconnected')
                break
            inc_lines = data.splitlines()
            try:
                uri = inc_lines[0].split()[1]
            except IndexError:
                break
            parts = 5  # how many things to send
            sleep_time = 0.2  # how long to sleep between parts
            close = True  # whether to close properly
            if uri[1:]:  # if there is something besides /
                uri_parts = uri[1:].split('/')
                if len(uri_parts) >= 1:
                    parts = int(uri_parts[0])
                if len(uri_parts) >= 2:
                    sleep_time = float(uri_parts[1])
                if len(uri_parts) >= 3:
                    close = json.loads(uri_parts[2])
            resp = ('HTTP/1.1 200 OK\r\n'
                    'X-Conn-Id: ' + str(conn_id) + '\r\n'
                    'Transfer-Encoding: chunked\r\n'
                    'Connection: keep-alive\r\n'
                    '\r\n')
            self.request.sendall(resp)
            for x in xrange(0, parts):
                self.request.sendall('{0}\r\n{1}\r\n'.format(len(str(x)), x))
                time.sleep(sleep_time)
            if close:
                self.request.sendall('0\r\n\r\n')
            else:
                self.request.sendall('lkfjasd;lfjas;d')

            time.sleep(2)


class TestChunked(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        '''
        This function is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''

        # create a socket server
        cls.port = tsqa.utils.bind_unused_port()[1]
        cls.server = tsqa.endpoint.SocketServerDaemon(ChunkedHandler, port=cls.port)
        cls.server.start()
        cls.server.ready.wait()

        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}/'.format(cls.port))

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.connect_attempts_timeout': 5,
            'proxy.config.http.connect_attempts_max_retries': 0,
            'proxy.config.http.keep_alive_enabled_in': 1,
            'proxy.config.http.keep_alive_enabled_out': 1,
            'proxy.config.exec_thread.limit': 1,
            'proxy.config.exec_thread.autoconfig': 0,
            'proxy.config.http.chunking_enabled': 1,
        })

    def test_chunked_origin(self):
        '''
        Test that the origin does in fact support keepalive
        '''
        self._client_test_chunked_keepalive(self.port)
        self._client_test_chunked_keepalive(self.port, num_bytes=2)
        self._client_test_chunked_keepalive(self.port, num_bytes=2, sleep=1)

    def _client_test_chunked_keepalive(self,
                                       port=None,
                                       times=3,
                                       num_bytes=None,
                                       sleep=None,
                                       ):
        if port is None:
            port = int(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(('127.0.0.1', port))

        url = '/'
        if num_bytes is not None:
            url += str(num_bytes)
        if sleep is not None:
            if num_bytes is None:
                raise Exception()
            url += '/' + str(sleep)

        request = ('GET ' + url + ' HTTP/1.1\r\n'
                   'Host: 127.0.0.1\r\n'
                   '\r\n')
        uuid = None
        # test basic
        for x in xrange(1, times):
            s.send(request)
            resp = ''
            while True:
                response = s.recv(4096)
                for line in response.splitlines():
                    line = line.strip()
                    if line.startswith('X-Conn-Id:'):
                        r_uuid = line.replace('X-Conn-Id:', '')
                        if uuid is None:
                            uuid = r_uuid
                        else:
                            self.assertEqual(uuid, r_uuid)
                resp += response
                if resp.endswith('\r\n0\r\n\r\n'):
                    break
            for x in xrange(0, num_bytes or 4):
                self.assertIn('1\r\n{0}\r\n'.format(x), resp)
        s.close()

    def test_chunked_basic(self):
        url = 'http://127.0.0.1:{0}'.format(self.port)
        ret = requests.get(url, proxies=self.proxies)
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.text.strip(), '01234')

    # TODO: fix keepalive with chunked responses
    def test_chunked_keepalive_server(self):
        url = 'http://127.0.0.1:{0}'.format(self.port)
        ret = requests.get(url, proxies=self.proxies)
        conn_id = ret.headers['x-conn-id']
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.text.strip(), '01234')

        # make sure that a second request works, and since we have keep-alive out
        # disabled it should be a new connection
        ret = requests.get(url, proxies=self.proxies)
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.text.strip(), '01234')
        self.assertEqual(conn_id, ret.headers['x-conn-id'])

    def test_chunked_keepalive_client(self):
        self._client_test_chunked_keepalive()
        self._client_test_chunked_keepalive(num_bytes=2)
        self._client_test_chunked_keepalive(num_bytes=2, sleep=1)

    def test_chunked_bad_close(self):
        url = 'http://127.0.0.1:{0}/5/0.1/false'.format(self.port)
        with self.assertRaises(socket.timeout):
            requests.get(url, proxies=self.proxies, timeout=2)
