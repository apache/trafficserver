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

import uuid
import requests
import time
import logging
import socket

import helpers

import tsqa.test_cases
import tsqa.utils
import tsqa.endpoint

log = logging.getLogger(__name__)

import SocketServer


class KeepaliveTCPHandler(SocketServer.BaseRequestHandler):
    """
    A subclass of RequestHandler which will return a connection uuid
    """

    def handle(self):
        # Receive the data in small chunks and retransmit it
        start = time.time()
        conn_id = uuid.uuid4().hex
        while True:
            now = time.time() - start
            data = self.request.recv(4096).strip()
            if data:
                log.debug('Sending data back to the client: {uid}'.format(uid=conn_id))
            else:
                log.debug('Client disconnected: {timeout}seconds'.format(timeout=now))
                break
            body = conn_id
            resp = ('HTTP/1.1 200 OK\r\n'
                    'Content-Length: {content_length}\r\n'
                    'Content-Type: text/html; charset=UTF-8\r\n'
                    'Connection: keep-alive\r\n'
                    '\r\n'
                    '{body}'.format(content_length=len(body), body=body))
            self.request.sendall(resp)


class KeepAliveInMixin(object):
    """Mixin for keep alive in.

       TODO: Allow protocol to be specified for ssl traffic
    """
    def _get_socket(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(('127.0.0.1', int(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])))
        return s

    def _headers_to_str(self, headers):
        if headers is None:
            headers = {}
        request = ''
        for k, v in headers.iteritems():
            request += '{0}: {1}\r\n'.format(k, v)
        return request

    def _aux_KA_working_path_connid(self, protocol, headers=None):
        if headers is None:
            headers = {}
        with requests.Session() as s:
            url = '{0}://127.0.0.1:{1}/'.format(protocol, int(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']))
            conn_id = None
            for x in xrange(1, 10):
                ret = s.get(url, headers=headers)
                self.assertEqual(ret.status_code, 200)
                if conn_id is None:
                    conn_id = ret.text
                else:
                    self.assertEqual(ret.text, conn_id)

    def _aux_working_path(self, protocol, headers=None):
        # connect tcp
        s = self._get_socket()

        request = ('GET /exists/ HTTP/1.1\r\n'
                   'Host: foobar.com\r\n')
        request += self._headers_to_str(headers)
        request += '\r\n'

        for x in xrange(1, 10):
            s.send(request)
            response = s.recv(4096)
            # cheat, since we know what the body should have
            if not response.endswith('hello'):
                response += s.recv(4096)
            self.assertIn('HTTP/1.1 200 OK', response)
            self.assertIn('hello', response)

    def _aux_error_path(self, protocol, headers=None):
        # connect tcp
        s = self._get_socket()

        request = ('GET / HTTP/1.1\r\n'
                   'Host: foobar.com\r\n')
        request += self._headers_to_str(headers)
        request += '\r\n'
        for x in xrange(1, 10):
            s.send(request)
            response = s.recv(4096)
            self.assertIn('HTTP/1.1 404 Not Found on Accelerator', response)

    def _aux_error_path_post(self, protocol, headers=None):
        '''
        Ensure that sending a request with a body doesn't break the keepalive session
        '''
        # connect tcp
        s = self._get_socket()

        request = ('POST / HTTP/1.1\r\n'
                   'Host: foobar.com\r\n'
                   'Content-Length: 10\r\n')
        request += self._headers_to_str(headers)
        request += '\r\n'
        request += '1234567890'

        for x in xrange(1, 10):
            try:
                s.send(request)
            except IOError:
                s = self._get_socket()
                s.send(request)

            response = s.recv(4096)
            # Check if client disconnected
            if response:
                self.assertIn('HTTP/1.1 404 Not Found on Accelerator', response)


class BasicTestsOutMixin(object):

    def _aux_KA_origin(self, protocol, headers=None):
        '''
        Test that the origin does in fact support keepalive
        '''
        conn_id = None
        with requests.Session() as s:
            url = '{0}://127.0.0.1:{1}/'.format(protocol, self.socket_server.port)
            for x in xrange(1, 10):
                ret = s.get(url, verify=False, headers=headers)
                if not conn_id:
                    conn_id = ret.text.strip()
                self.assertEqual(ret.status_code, 200)
                self.assertEqual(ret.text.strip(), conn_id, "Client reports server closed connection")

    def _aux_KA_proxy(self, protocol, headers=None):
        '''
        Test that keepalive works through ATS to that origin
        '''
        url = '{0}://127.0.0.1:{1}'.format(
            protocol,
            self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'],
        )
        conn_id = None
        for x in xrange(1, 10):
            ret = requests.get(url, verify=False, headers=headers)
            if not conn_id:
                conn_id = ret.text.strip()
            self.assertEqual(ret.status_code, 200)
            self.assertEqual(ret.text.strip(), conn_id, "Client reports server closed connection")


class TimeoutOutMixin(object):

    def _aux_KA_timeout_direct(self, protocol):
        '''Tests that origin does not timeout using keepalive.'''
        with requests.Session() as s:
            url = '{0}://127.0.0.1:{1}/'.format(protocol, self.socket_server.port)
            conn_id = None
            for x in xrange(0, 3):
                ret = s.get(url, verify=False)
                if not conn_id:
                    conn_id = ret.text.strip()
                self.assertEqual(ret.text.strip(), conn_id, "Client reports server closed connection")
                time.sleep(3)

    def _aux_KA_timeout_proxy(self, protocol):
        '''Tests that keepalive timeout is honored through ATS to origin.'''
        url = '{0}://127.0.0.1:{1}'.format(
            protocol,
            self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'],
        )
        conn_id = None
        for x in xrange(0, 3):
            ret = requests.get(url, verify=False)
            if not conn_id:
                conn_id = ret.text.strip()
            self.assertEqual(ret.text.strip(), conn_id, "Client reports server closed connection")
            time.sleep(3)


class OriginMinMaxMixin(object):

    def _aux_KA_min_origin(self, protocol):
        '''Tests that origin_min_keep_alive_connections is honored.'''
        url = '{0}://127.0.0.1:{1}'.format(
            protocol,
            self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'],
        )
        ret = requests.get(url, verify=False)
        conn_id = ret.text.strip()
        time.sleep(3)
        ret = requests.get(url, verify=False)
        self.assertEqual(ret.text.strip(), conn_id, "Client reports server closed connection")


class TestKeepAliveInHTTP(tsqa.test_cases.DynamicHTTPEndpointCase, helpers.EnvironmentCase, KeepAliveInMixin):
    @classmethod
    def setUpEnv(cls, env):

        def hello(request):
            return 'hello'
        cls.http_endpoint.add_handler('/exists/', hello)

        cls.configs['remap.config'].add_line('map /exists/ http://127.0.0.1:{0}/exists/'.format(cls.http_endpoint.address[1]))

        # only add server headers when there weren't any
        cls.configs['records.config']['CONFIG']['proxy.config.http.response_server_enabled'] = 2
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_enabled_in'] = 1
        cls.configs['records.config']['CONFIG']['share_server_session'] = 2

        # set only one ET_NET thread (so we don't have to worry about the per-thread pools causing issues)
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.limit'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.autoconfig'] = 0

    def test_working_path(self):
        self._aux_working_path("http")

    def test_error_path(self):
        self._aux_error_path("http")

    def test_error_path_post(self):
        '''
        Ensure that sending a request with a body doesn't break the keepalive session
        '''
        self._aux_error_path_post("http")


class TestKeepAliveOriginConnOutHTTP(helpers.EnvironmentCase, OriginMinMaxMixin):
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
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}/'.format(cls.socket_server.port))

        # only add server headers when there weren't any
        cls.configs['records.config']['CONFIG']['proxy.config.http.response_server_enabled'] = 2
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_enabled_out'] = 1
        cls.configs['records.config']['CONFIG']['share_server_session'] = 2

        # set only one ET_NET thread (so we don't have to worry about the per-thread pools causing issues)
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.limit'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.autoconfig'] = 0

        # Timeouts
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_no_activity_timeout_out'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.http.transaction_no_activity_timeout_out'] = 1

        cls.configs['records.config']['CONFIG']['proxy.config.http.origin_min_keep_alive_connections'] = 1

    def test_KA_min_origin(self):
        '''Tests that origin_min_keep_alive_connections is honored via http.'''
        self._aux_KA_min_origin("http")


class TestKeepAliveOriginConnOutHTTPS(helpers.EnvironmentCase, OriginMinMaxMixin):
    @classmethod
    def setUpEnv(cls, env):
        '''
        This function is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''
        # create a socket server
        cls.socket_server = tsqa.endpoint.SSLSocketServerDaemon(
            KeepaliveTCPHandler,
            helpers.tests_file_path('cert.pem'),
            helpers.tests_file_path('key.pem'),
        )
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

        # Timeouts
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_no_activity_timeout_out'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.http.transaction_no_activity_timeout_out'] = 1

        cls.configs['records.config']['CONFIG']['proxy.config.http.origin_min_keep_alive_connections'] = 1

    def test_KA_min_origin(self):
        '''Tests that origin_min_keep_alive_connections is honored via https.'''
        self._aux_KA_min_origin("http")


class TestKeepAliveOutHTTP(helpers.EnvironmentCase, BasicTestsOutMixin, TimeoutOutMixin):
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
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}/'.format(cls.socket_server.port))

        # only add server headers when there weren't any
        cls.configs['records.config']['CONFIG']['proxy.config.http.response_server_enabled'] = 2
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_enabled_out'] = 1
        cls.configs['records.config']['CONFIG']['share_server_session'] = 2

        # set only one ET_NET thread (so we don't have to worry about the per-thread pools causing issues)
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.limit'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.autoconfig'] = 0

        # Timeouts
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_no_activity_timeout_out'] = 10
        cls.configs['records.config']['CONFIG']['proxy.config.http.transaction_no_activity_timeout_out'] = 2

    def test_KA_origin(self):
        '''Test that the origin does in fact support keepalive via http.'''
        self._aux_KA_origin("http")

    def test_KA_proxy(self):
        '''Tests that keepalive works through ATS to origin via http.'''
        self._aux_KA_proxy("http")

    def test_KA_timeout_direct(self):
        '''Tests that origin does not timeout using keepalive via http.'''
        self._aux_KA_timeout_direct("http")

    def test_KA_timeout_proxy(self):
        '''Tests that keepalive timeout is honored through ATS to origin via http.'''
        self._aux_KA_timeout_proxy("http")


class TestKeepAliveOutHTTPS(helpers.EnvironmentCase, BasicTestsOutMixin, TimeoutOutMixin):
    @classmethod
    def setUpEnv(cls, env):
        '''
        This function is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''
        # create a socket server
        cls.socket_server = tsqa.endpoint.SSLSocketServerDaemon(
            KeepaliveTCPHandler,
            helpers.tests_file_path('cert.pem'),
            helpers.tests_file_path('key.pem'),
        )
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

        # Timeouts
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_no_activity_timeout_out'] = 10
        cls.configs['records.config']['CONFIG']['proxy.config.http.transaction_no_activity_timeout_out'] = 2

    def test_KA_origin(self):
        '''Test that the origin does in fact support keepalive via https.'''
        self._aux_KA_origin("https")

    def test_KA_proxy(self):
        '''Tests that keepalive works through ATS to origin via https.'''
        self._aux_KA_proxy("http")

    def test_KA_timeout_direct(self):
        '''Tests that origin does not timeout using keepalive via https.'''
        self._aux_KA_timeout_direct("https")

    def test_KA_timeout_proxy(self):
        '''Tests that keepalive timeout is honored through ATS to origin via https.'''
        self._aux_KA_timeout_proxy("http")


# TODO: refactor these tests, these are *very* similar, we should paramatarize them
# Some basic tests for auth_sever_session_private
class TestKeepAlive_Authorization_private(helpers.EnvironmentCase, BasicTestsOutMixin, KeepAliveInMixin):
    @classmethod
    def setUpEnv(cls, env):

        cls.socket_server = tsqa.endpoint.SocketServerDaemon(KeepaliveTCPHandler)
        cls.socket_server.start()
        cls.socket_server.ready.wait()
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}/exists/'.format(cls.socket_server.port))

        # only add server headers when there weren't any
        cls.configs['records.config']['CONFIG']['proxy.config.http.response_server_enabled'] = 2
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_enabled_in'] = 1
        cls.configs['records.config']['CONFIG']['share_server_session'] = 2

        # set only one ET_NET thread (so we don't have to worry about the per-thread pools causing issues)
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.limit'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.autoconfig'] = 0

        # make auth sessions private
        cls.configs['records.config']['CONFIG']['proxy.config.auth_server_session_private'] = 1

    def test_KA_server(self):
        '''Tests that keepalive works through ATS to origin via https.'''
        with self.assertRaises(AssertionError):
            self._aux_KA_proxy("http", headers={'Authorization': 'Foo'})

    def test_KA_client(self):
        '''Tests that keepalive works through ATS to origin via https.'''
        with self.assertRaises(AssertionError):
            self._aux_KA_working_path_connid("http", headers={'Authorization': 'Foo'})


class TestKeepAlive_Authorization_no_private(helpers.EnvironmentCase, BasicTestsOutMixin, KeepAliveInMixin):
    @classmethod
    def setUpEnv(cls, env):

        cls.socket_server = tsqa.endpoint.SocketServerDaemon(KeepaliveTCPHandler)
        cls.socket_server.start()
        cls.socket_server.ready.wait()
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}/exists/'.format(cls.socket_server.port))

        # only add server headers when there weren't any
        cls.configs['records.config']['CONFIG']['proxy.config.http.response_server_enabled'] = 2
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_enabled_in'] = 1
        cls.configs['records.config']['CONFIG']['share_server_session'] = 2

        # set only one ET_NET thread (so we don't have to worry about the per-thread pools causing issues)
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.limit'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.autoconfig'] = 0

        # make auth sessions private
        cls.configs['records.config']['CONFIG']['proxy.config.http.auth_server_session_private'] = 0

    def test_KA_server(self):
        '''Tests that keepalive works through ATS to origin via https.'''
        self._aux_KA_proxy("http", headers={'Authorization': 'Foo'})

    def test_KA_client(self):
        '''Tests that keepalive works through ATS to origin via https.'''
        self._aux_KA_working_path_connid("http", headers={'Authorization': 'Foo'})
