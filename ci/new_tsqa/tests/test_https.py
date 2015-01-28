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

import logging
from OpenSSL import SSL
import socket

import helpers
import tsqa.utils


class TestSSL(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        '''
        This function is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''

        # add an SSL port to ATS
        cls.ssl_port = tsqa.utils.bind_unused_port()[1]
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:ssl'.format(cls.ssl_port)
        cls.configs['records.config']['CONFIG']['proxy.config.diags.debug.enabled'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.diags.debug.tags'] = 'ssl'

        # configure SSL multicert
        cls.configs['ssl_multicert.config'].add_line('dest_ip=127.0.0.2 ssl_cert_name={0}'.format(helpers.tests_file_path('rsa_keys/www.example.com.pem')))
        cls.configs['ssl_multicert.config'].add_line('dest_ip=127.0.0.2 ssl_cert_name={0}'.format(helpers.tests_file_path('rsa_keys/www.test.com.pem')))

        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0}'.format(helpers.tests_file_path('rsa_keys/www.example.com.pem')))
        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0}'.format(helpers.tests_file_path('rsa_keys/www.test.com.pem')))

    def _get_cert(self, addr, sni_name=None):
        '''
        Return the certificate for addr. Optionally sending sni_name
        '''
        ctx = SSL.Context(SSL.SSLv23_METHOD)
        # Set up client
        sock = SSL.Connection(ctx, socket.socket(socket.AF_INET, socket.SOCK_STREAM))
        sock.connect(addr)
        if sni_name is not None:
            sock.set_tlsext_host_name(sni_name)
        sock.do_handshake()
        return sock.get_peer_certificate()

    def test_star_ordering(self):
        '''
        We should be served the first match, since we aren't sending SNI headers
        '''
        addr = ('127.0.0.1', self.ssl_port)
        cert = self._get_cert(addr)
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.example.com')

    def test_star_sni(self):
        '''
        Make sure we get the certificate we asked for if we pass in SNI headers
        '''
        addr = ('127.0.0.1', self.ssl_port)
        cert = self._get_cert(addr, sni_name='www.test.com')
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.test.com')

        cert = self._get_cert(addr, sni_name='www.example.com')
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.example.com')

    def test_ip_ordering(self):
        '''
        We should be served the first match, since we aren't sending SNI headers
        '''
        addr = ('127.0.0.2', self.ssl_port)
        cert = self._get_cert(addr)
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.example.com')

    def test_ip_sni(self):
        '''
        Make sure we get the certificate we asked for if we pass in SNI headers
        '''
        addr = ('127.0.0.2', self.ssl_port)
        cert = self._get_cert(addr, sni_name='www.test.com')
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.test.com')

        cert = self._get_cert(addr, sni_name='www.example.com')
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.example.com')
