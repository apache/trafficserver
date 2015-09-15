'''
Test hostdb
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
import requests
import time
import logging
import SocketServer

import tsqa.test_cases
import helpers

log = logging.getLogger(__name__)


class EchoServerIpHandler(SocketServer.BaseRequestHandler):
    """
    A subclass of RequestHandler which will return a connection uuid
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
            resp = ('HTTP/1.1 200 OK\r\n'
                    'Content-Length: 0\r\n'
                    'Content-Type: text/html; charset=UTF-8\r\n'
                    'Connection: keep-alive\r\n'
                    'X-Server-Ip: {server_ip}\r\n'
                    'X-Server-Port: {server_port}\r\n'
                    '\r\n'.format(
                        server_ip=self.request.getsockname()[0],
                        server_port=self.request.getsockname()[0],
                    ))
            self.request.sendall(resp)


class TestHostDBBadResolvConf(helpers.EnvironmentCase):
    '''
    Test that ATS can handle an empty resolv_conf
    '''
    @classmethod
    def setUpEnv(cls, env):
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.response_server_enabled': 2,  # only add server headers when there weren't any
            'proxy.config.hostdb.lookup_timeout': 2,
            'proxy.config.dns.resolv_conf': '/tmp/non_existant_file',
            'proxy.config.url_remap.remap_required': 0,

        })

    def test_working(self):
        ret = requests.get('http://trafficserver.readthedocs.org',
                           proxies=self.proxies,
                           )
        self.assertEqual(ret.status_code, 502)


class TestHostDBPartiallyFailedDNS(helpers.EnvironmentCase):
    '''
    Tests for how hostdb handles when there is one failed and one working resolver
    '''
    @classmethod
    def setUpEnv(cls, env):
        # TODO: Fix this!
        # This intermittently fails on Jenkins (such as https://ci.trafficserver.apache.org/job/tsqa-master/387/testReport/test_hostdb/TestHostDBPartiallyFailedDNS/test_working/)
        # we aren't sure if this is a failure of ATS or just a race on jenkins (since its slow)
        raise helpers.unittest.SkipTest()

        resolv_conf_path = os.path.join(env.layout.prefix, 'resolv.conf')

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.response_server_enabled': 2,  # only add server headers when there weren't any
            'proxy.config.hostdb.lookup_timeout': 2,
            'proxy.config.dns.resolv_conf': resolv_conf_path,
            'proxy.config.url_remap.remap_required': 0,

        })

        with open(resolv_conf_path, 'w') as fh:
            fh.write('nameserver 1.1.1.0\n')  # some non-existant nameserver
            fh.write('nameserver 8.8.8.8\n')  # some REAL nameserver

    def test_working(self):
        start = time.time()
        ret = requests.get('http://trafficserver.readthedocs.org',
                           proxies=self.proxies,
                           )
        self.assertLess(time.time() - start, self.configs['records.config']['CONFIG']['proxy.config.hostdb.lookup_timeout'])
        self.assertEqual(ret.status_code, 200)


class TestHostDBFailedDNS(helpers.EnvironmentCase):
    '''
    Tests for how hostdb handles when there is no reachable resolver
    '''
    @classmethod
    def setUpEnv(cls, env):
        resolv_conf_path = os.path.join(env.layout.prefix, 'resolv.conf')

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.response_server_enabled': 2,  # only add server headers when there weren't any
            'proxy.config.hostdb.lookup_timeout': 2,
            'proxy.config.dns.resolv_conf': resolv_conf_path,
            'proxy.config.url_remap.remap_required': 0,

        })

        with open(resolv_conf_path, 'w') as fh:
            fh.write('nameserver 1.1.1.0\n')  # some non-existant nameserver

    def test_lookup_timeout(self):
        start = time.time()
        ret = requests.get('http://some_nonexistant_domain',
                           proxies=self.proxies,
                           )
        self.assertGreater(time.time() - start, self.configs['records.config']['CONFIG']['proxy.config.hostdb.lookup_timeout'])
        self.assertEqual(ret.status_code, 502)
        self.assertIn('ATS', ret.headers['server'])


class TestHostDBHostsFile(helpers.EnvironmentCase, tsqa.test_cases.HTTPBinCase):
    '''
    Tests for hostdb's host-file implementation
    '''
    @classmethod
    def setUpEnv(cls, env):
        cls.hosts_file_path = os.path.join(env.layout.prefix, 'hosts')
        with open(cls.hosts_file_path, 'w') as fh:
            fh.write('127.0.0.1 local\n')
            fh.write('127.0.0.2 local2\n')

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.response_server_enabled': 2,  # only add server headers when there weren't any
            'proxy.config.hostdb.lookup_timeout': 2,
            'proxy.config.url_remap.remap_required': 1,
            'proxy.config.http.connect_attempts_max_retries': 1,
            'proxy.config.hostdb.host_file.interval': 1,
            'proxy.config.hostdb.host_file.path': cls.hosts_file_path,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'hostdb',
        })
        # create a socket server
        cls.socket_server = tsqa.endpoint.SocketServerDaemon(EchoServerIpHandler)
        cls.socket_server.start()
        cls.socket_server.ready.wait()
        cls.configs['remap.config'].add_line('map http://local/ http://local:{0}/'.format(cls.socket_server.port))
        cls.configs['remap.config'].add_line('map http://local2/ http://local2:{0}/'.format(cls.socket_server.port))
        cls.configs['remap.config'].add_line('map http://local3/ http://local3:{0}/'.format(cls.socket_server.port))

    def test_basic(self):
        '''
        Test basic fnctionality of hosts files
        '''
        # TODO add stat, then wait for the stat to increment
        time.sleep(2)  # wait for the continuation to load the hosts file
        ret = requests.get(
            'http://local/get',
            proxies=self.proxies,
        )
        self.assertEqual(ret.status_code, 200)
        self.assertEqual('127.0.0.1', ret.headers['X-Server-Ip'])

        ret = requests.get(
            'http://local2/get',
            proxies=self.proxies,
        )
        self.assertEqual(ret.status_code, 200)
        self.assertEqual('127.0.0.2', ret.headers['X-Server-Ip'])

    def test_reload(self):
        '''
        Test that changes to hosts file get loaded within host_file.interval
        '''
        # TODO add stat, then wait for the stat to increment
        time.sleep(2)  # wait for the continuation to load the hosts file
        ret = requests.get(
            'http://local3/get',
            proxies=self.proxies,
        )
        self.assertEqual(ret.status_code, 502)

        with open(self.hosts_file_path, 'a') as fh:
            fh.write('127.0.0.3 local3\n')

        # TODO add stat, then wait for the stat to increment, with a timeout
        time.sleep(2)

        ret = requests.get(
            'http://local3/get',
            proxies=self.proxies,
        )
        self.assertEqual(ret.status_code, 200)
        self.assertEqual('127.0.0.3', ret.headers['X-Server-Ip'])
