'''
Test Origin Server Connect Attempts
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
import socket
import struct
import select
import threading

import helpers

log = logging.getLogger(__name__)


def thread_die_on_connect(sock):
    sock.listen(0)
    # poll
    select.select([sock], [], [])
    # exit
    sock.close()


def thread_reset_after_accept(sock):
    sock.listen(0)
    first = True
    num_requests = 0
    while True:
        connection, addr = sock.accept()
        num_requests += 1
        if first:
            first = False
            connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
            connection.close()
        else:
            connection.send((
                'HTTP/1.1 200 OK\r\n'
                'Content-Length: {body_len}\r\n'
                'Content-Type: text/html; charset=UTF-8\r\n'
                'Connection: close\r\n\r\n{body}'.format(body_len=len(str(num_requests)), body=num_requests)
            ))
            connection.close()


def thread_partial_response(sock):
    sock.listen(0)
    first = True
    num_requests = 0
    while True:
        connection, addr = sock.accept()
        num_requests += 1
        if first:
            connection.send('HTTP/1.1 500 Internal Server Error\r\n')
            connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
            connection.close()
            first = False
        else:
            connection.send((
                'HTTP/1.1 200 OK\r\n'
                'Content-Length: {body_len}\r\n'
                'Content-Type: text/html; charset=UTF-8\r\n'
                'Connection: close\r\n\r\n{body}'.format(body_len=len(str(num_requests)), body=num_requests)
            ))
            connection.close()


def thread_slow_response(sock):
    '''
    Thread to sleep a decreasing amount of time before sending the response

    sleep times: 2 -> 1 -> 0
    '''
    sock.listen(0)
    sleep_time = 2
    num_requests = 0
    # poll
    while True:
        select.select([sock], [], [])
        try:
            connection, addr = sock.accept()
            time.sleep(sleep_time)
            connection.send((
                'HTTP/1.1 200 OK\r\n'
                'Content-Length: {body_len}\r\n'
                'Content-Type: text/html; charset=UTF-8\r\n'
                'Connection: close\r\n\r\n{body}'.format(body_len=len(str(num_requests)), body=num_requests)
            ))
            connection.close()
            num_requests += 1
        except Exception as e:
            print 'connection died!', e
            pass
        if sleep_time > 0:
            sleep_time -= 1


def thread_slow_close(sock):
    '''
    Thread to sleep a decreasing amount of time after the request, before closing

    sleep times: 2 -> 1 -> 0
    '''
    sock.listen(0)
    sleep_time = 2
    num_requests = 0
    # poll
    while True:
        select.select([sock], [], [])
        try:
            connection, addr = sock.accept()
            connection.send((
                'HTTP/1.1 200 OK\r\n'
                'Content-Length: {body_len}\r\n'
                'Content-Type: text/html; charset=UTF-8\r\n'
                'Connection: close\r\n\r\n{body}'.format(body_len=len(str(num_requests)), body=num_requests)
            ))
            time.sleep(sleep_time)
            connection.close()
            num_requests += 1
        except Exception as e:
            print 'connection died!', e
            pass
        if sleep_time > 0:
            sleep_time -= 1


class TestOriginServerConnectAttempts(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        '''
        This function is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''
        cls.sock_map = {}

        def _add_sock(name):
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.bind(('127.0.0.1', 0))
            cls.sock_map[name] = sock.getsockname()[1]
            cls.configs['remap.config'].add_line('map /{0}/ http://127.0.0.1:{1}/'.format(name, cls.sock_map[name]))
            return sock
        # create a socket where we just bind
        _add_sock('bound')

        # create a socket where we bind + listen
        sock = _add_sock('listen')
        sock.listen(1)

        # create a bunch of special socket servers
        sock = _add_sock('die_on_connect')
        t = threading.Thread(target=thread_die_on_connect, args=(sock,))
        t.daemon = True
        t.start()

        sock = _add_sock('reset_after_accept')
        t = threading.Thread(target=thread_reset_after_accept, args=(sock,))
        t.daemon = True
        t.start()

        sock = _add_sock('slow_response')
        t = threading.Thread(target=thread_slow_response, args=(sock,))
        t.daemon = True
        t.start()

        sock = _add_sock('partial_response')
        t = threading.Thread(target=thread_partial_response, args=(sock,))
        t.daemon = True
        t.start()

        sock = _add_sock('slow_close')
        t = threading.Thread(target=thread_slow_close, args=(sock,))
        t.daemon = True
        t.start()

        # only add server headers when there weren't any
        cls.configs['records.config']['CONFIG']['proxy.config.http.response_server_enabled'] = 2

        # enable re-connects, timeout of 1s, max retires of 3
        cls.configs['records.config']['CONFIG']['proxy.config.http.connect_attempts_timeout'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.http.connect_attempts_max_retries'] = 3

    def test_bound_origin(self):
        '''Verify that we get 502s from an origin which just did a bind'''
        url = 'http://127.0.0.1:{0}/bound/s'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        ret = requests.get(url)
        self.assertEqual(ret.status_code, 502)

    def test_listen_origin(self):
        '''Verify that we get 502s from origins that bind + listen'''
        url = 'http://127.0.0.1:{0}/listen/s'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        ret = requests.get(url)
        self.assertEqual(ret.status_code, 502)

        url = 'http://127.0.0.1:{0}/listen/s'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        ret = requests.get(url)
        self.assertEqual(ret.status_code, 502)

    def test_die_on_connect_origin(self):
        '''Verify that we get 504s from origins that die_on_connect'''
        url = 'http://127.0.0.1:{0}/die_on_connect/s'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        ret = requests.get(url)
        self.assertEqual(ret.status_code, 504)

    def test_partial_response_origin(self):
        '''
        Verify that we get 504s from origins that return a partial_response

        We want to bail out-- since the origin already got the request, we can't
        gaurantee that the request is re-entrant
        '''
        url = 'http://127.0.0.1:{0}/partial_response/s'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        ret = requests.get(url)
        self.assertEqual(ret.status_code, 500)

    def test_reset_after_accept_origin(self):
        '''Verify that we get 502s from origins that reset_after_accept, once any bytes are sent to origin we assume we cannot re-dispatch'''
        url = 'http://127.0.0.1:{0}/reset_after_accept/s'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        ret = requests.get(url)
        self.assertEqual(ret.status_code, 502)

    def test_slow_response(self):
        '''Verify that we get 5xx from origins that take longer than acceptable, since we will not retry them'''
        url = 'http://127.0.0.1:{0}/slow_response/s'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        ret = requests.get(url)
        # make sure it worked
        self.assertEqual(ret.status_code, 504)

    def test_slow_close(self):
        '''Verify that we retry connecting to an origin when there is a connection failure'''
        url = 'http://127.0.0.1:{0}/slow_close/s'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        ret = requests.get(url)
        # make sure it worked
        self.assertEqual(ret.status_code, 200)
