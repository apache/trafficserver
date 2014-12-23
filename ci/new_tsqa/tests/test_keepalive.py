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

import os
import requests
import time
import logging

import helpers

import tsqa.test_cases
import tsqa.utils
import tsqa.endpoint

log = logging.getLogger(__name__)

import SocketServer
class KeepaliveTCPHandler(SocketServer.BaseRequestHandler):
    """
    A subclass of RequestHandler which will count the number of requests
    per tcp session
    """

    def handle(self):
        num_requests = 0
        # Receive the data in small chunks and retransmit it
        while True:
            num_requests += 1
            data = self.request.recv(4096).strip()
            if data:
                log.info('sending data back to the client')
            else:
                log.info('Client disconnected')
                break
            body = str(num_requests)
            resp = ('HTTP/1.1 200 OK\r\n'
                    'Content-Length: {content_length}\r\n'
                    'Content-Type: text/html; charset=UTF-8\r\n'
                    'Connection: keep-alive\r\n'
                    '\r\n'
                    '{body}'.format(content_length=len(body), body=body))
            self.request.sendall(resp)

# TODO: test timeouts
# https://issues.apache.org/jira/browse/TS-3312
# https://issues.apache.org/jira/browse/TS-242


class TestKeepAliveOutHTTP(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        '''
        This function is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''
        # create a socket server
        cls.socket_server = tsqa.endpoint.SocketServerDaemon(KeepaliveTCPHandler)
        cls.socket_server.start()
        cls.socket_server.ready.wait()
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}/\n'.format(cls.socket_server.port))

        # only add server headers when there weren't any
        cls.configs['records.config']['CONFIG']['proxy.config.http.response_server_enabled'] = 2
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_enabled_out'] = 1
        cls.configs['records.config']['CONFIG']['share_server_session'] = 2

        # set only one ET_NET thread (so we don't have to worry about the per-thread pools causing issues)
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.limit'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.autoconfig'] = 0

    def test_KA_origin(self):
        '''
        Test that the origin does in fact support keepalive
        '''
        with requests.Session() as s:
            url = 'http://127.0.0.1:{0}/'.format(self.socket_server.port)
            for x in xrange(1, 10):
                ret = s.get(url)
                self.assertEqual(ret.status_code, 200)
                self.assertEqual(ret.text.strip(), str(x))

    def test_KA_proxy(self):
        '''
        Test that keepalive works through ATS to that origin
        '''
        url = 'http://127.0.0.1:{0}'.format(self.socket_server.port)
        for x in xrange(1, 10):
            ret = requests.get(url, proxies=self.proxies)
            self.assertEqual(ret.status_code, 200)
            self.assertEqual(ret.text.strip(), str(x))

class TestKeepAliveOutHTTPS(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        '''
        This function is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''
        # create a socket server
        cls.socket_server = tsqa.endpoint.SSLSocketServerDaemon(KeepaliveTCPHandler,
                                             helpers.tests_file_path('cert.pem'),
                                             helpers.tests_file_path('key.pem'))

        cls.socket_server.start()
        cls.socket_server.ready.wait()
        cls.configs['remap.config'].add_line('map / https://127.0.0.1:{0}/\n'.format(cls.socket_server.port))

        # only add server headers when there weren't any
        cls.configs['records.config']['CONFIG']['proxy.config.http.response_server_enabled'] = 2
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_enabled_out'] = 1
        cls.configs['records.config']['CONFIG']['share_server_session'] = 2

        # set only one ET_NET thread (so we don't have to worry about the per-thread pools causing issues)
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.limit'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.autoconfig'] = 0

        cls.configs['records.config']['CONFIG']['proxy.config.ssl.number.threads'] = -1


    def test_KA_origin(self):
        '''
        Test that the origin does in fact support keepalive
        '''
        with requests.Session() as s:
            url = 'https://127.0.0.1:{0}/'.format(self.socket_server.port)
            for x in xrange(1, 10):
                ret = s.get(url, verify=False)
                self.assertEqual(ret.status_code, 200)
                self.assertEqual(ret.text.strip(), str(x))

    def test_KA_proxy(self):
        '''
        Test that keepalive works through ATS to that origin
        '''
        url = 'http://127.0.0.1:{0}'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        for x in xrange(1, 10):
            ret = requests.get(url)
            self.assertEqual(ret.status_code, 200)
            self.assertEqual(ret.text.strip(), str(x))
