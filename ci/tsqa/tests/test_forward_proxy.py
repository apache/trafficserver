'''
Test Forward Proxy
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
import os
import logging
import socket
import select
import threading
import helpers

log = logging.getLogger(__name__)


def thread_dummy_ftp(sock):
    '''
    Thread to create a dummy ftp over http response using a socket
    '''
    sock.listen(0)
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
            connection.close()
            num_requests += 1
        except Exception as e:
            print 'connection died!', e
            pass

class FTPHelper(helpers.EnvironmentCase):
    @staticmethod
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
            return sock
        # create a socket where we just bind
        _add_sock('bound')

        # create a socket where we bind + listen
        sock = _add_sock('listen')
        sock.listen(1)


        sock = _add_sock('dummy_ftp')
        t = threading.Thread(target=thread_dummy_ftp, args=(sock,))
        t.daemon = True
        t.start()
	# forward proxy
        cls.configs['records.config']['CONFIG']['proxy.config.url_remap.remap_required'] = 0
        # Optional strictly forward proxy setting
        cls.configs['records.config']['CONFIG']['proxy.config.reverse_proxy.enabled'] = 0
        cls.configs['records.config']['CONFIG']['proxy.config.http.response_server_enabled'] = 2
        # only add server headers when there weren't any
        cls.configs['records.config']['CONFIG']['proxy.config.http.response_server_enabled'] = 2

        # enable re-connects, timeout of 1s, max retires of 3
        cls.configs['records.config']['CONFIG']['proxy.config.http.connect_attempts_timeout'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.http.connect_attempts_max_retries'] = 3

    @staticmethod
    def useDummyFtpSocket(self):
        '''Verify that we connect via ftp that there is a connection failure'''
        proxy_port=self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']
        url = 'GET ftp://127.0.0.1:{0}/dummy_ftp/s HTTP/1.1\r\nHost: 127.0.0.1:{0}\r\n\r\n'.format(self.sock_map['dummy_ftp'])
        ftpSocket = socket.socket()
        ftpSocket.connect(('127.0.0.1',int(proxy_port)))
        ftpSocket.send(url.encode())
        data = ftpSocket.recv(1024)
        gen = data.decode('utf-8')
        status=format(gen.split('\r\n')[0].split(' ')[1])
        ftpSocket.close()
        return status

class TestOriginServerFtpEnabled(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        FTPHelper.setUpEnv(cls,env)
        cls.configs['records.config']['CONFIG']['proxy.config.ftp_enabled'] = 1

    def test_dummy_ftp(self):
        status=FTPHelper.useDummyFtpSocket(self)
        self.assertEqual(int(status), 200)

class TestOriginServerFtpDisabled(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        FTPHelper.setUpEnv(cls,env)
        cls.configs['records.config']['CONFIG']['proxy.config.ftp_enabled'] = 0

    def test_dummy_ftp(self):
        status=FTPHelper.useDummyFtpSocket(self)
        self.assertEqual(int(status), 400)

