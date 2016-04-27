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

from OpenSSL import SSL
import socket
import time
import helpers
import tsqa.utils
import os
import logging
unittest = tsqa.utils.import_unittest()

log = logging.getLogger(__name__)
# some ciphers to test with
CIPHER_MAP = {
    'rsa': 'ECDHE-RSA-AES256-GCM-SHA384',
    'ecdsa': 'ECDHE-ECDSA-AES256-GCM-SHA384',
}


class CertSelectionMixin(object):
    def _get_cert(self, addr, sni_name=None, ciphers=None):
        '''
        Return the certificate for addr. Optionally sending sni_name
        '''
        ctx = SSL.Context(SSL.TLSv1_2_METHOD)
        # Set up client
        sock = SSL.Connection(ctx, socket.socket(socket.AF_INET, socket.SOCK_STREAM))
        sock.connect(addr)
        if sni_name is not None:
            sock.set_tlsext_host_name(sni_name)
        if ciphers is not None:
            ctx.set_cipher_list(ciphers)
        sock.do_handshake()
        return sock.get_peer_certificate()

    def _get_cert_chain(self, addr, sni_name=None, ciphers=None):
        '''
        Return the certificate chain for addr. Optionally sending sni_name
        '''
        ctx = SSL.Context(SSL.TLSv1_2_METHOD)
        # Set up client
        sock = SSL.Connection(ctx, socket.socket(socket.AF_INET, socket.SOCK_STREAM))
        sock.connect(addr)
        if sni_name is not None:
            sock.set_tlsext_host_name(sni_name)
        if ciphers is not None:
            ctx.set_cipher_list(ciphers)
        sock.do_handshake()
        return sock.get_peer_cert_chain()

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

    def _intermediate_ca_t(self, cipher):
        '''
        Method for testing intermediate CAs. We assume that www.example.com should
        return a certificate chaing of len 2 which includes intermediate.
        We also assume that www.test.com returns a single cert in the chain which
        is *not* intermediate
        '''
        # send a request that *should* get an intermediate CA
        addr = ('127.0.0.1', self.ssl_port)
        cert_chain = self._get_cert_chain(addr, ciphers=CIPHER_MAP[cipher])
        self.assertEqual(len(cert_chain), 2)
        self.assertEqual(cert_chain[0].get_subject().commonName.decode(), 'www.example.com')
        self.assertEqual(cert_chain[1].get_subject().commonName.decode(), 'intermediate')

        # send a request that shouldn't get an intermediate CA
        addr = ('127.0.0.1', self.ssl_port)
        cert_chain = self._get_cert_chain(addr, ciphers=CIPHER_MAP[cipher], sni_name='www.test.com')
        self.assertEqual(len(cert_chain), 1)
        self.assertEqual(cert_chain[0].get_subject().commonName.decode(), 'www.test.com')


class TestRSA(helpers.EnvironmentCase, CertSelectionMixin):
    '''
    Tests for https for ATS configured with RSA certificates
    '''
    @classmethod
    def setUpEnv(cls, env):
        # add an SSL port to ATS
        cls.ssl_port = tsqa.utils.bind_unused_port()[1]
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:ssl'.format(cls.ssl_port)
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'ssl',
            'proxy.config.ssl.server.cipher_suite': CIPHER_MAP['rsa'],
        })

        # configure SSL multicert
        cls.configs['ssl_multicert.config'].add_line('dest_ip=127.0.0.2 ssl_cert_name={0} ssl_ca_name={1}'.format(
            helpers.tests_file_path('rsa_keys/www.example.com.pem'),
            helpers.tests_file_path('rsa_keys/intermediate.crt'),
        ))
        cls.configs['ssl_multicert.config'].add_line('dest_ip=127.0.0.2 ssl_cert_name={0}'.format(
            helpers.tests_file_path('rsa_keys/www.test.com.pem'),
        ))

        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0} ssl_ca_name={1}'.format(
            helpers.tests_file_path('rsa_keys/www.example.com.pem'),
            helpers.tests_file_path('rsa_keys/intermediate.crt'),
        ))
        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0}'.format(
            helpers.tests_file_path('rsa_keys/www.test.com.pem'),
        ))

    def test_rsa(self):
        addr = ('127.0.0.1', self.ssl_port)
        cert = self._get_cert(addr, ciphers=CIPHER_MAP['rsa'])
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.example.com')

    def test_ecdsa(self):
        addr = ('127.0.0.1', self.ssl_port)
        with self.assertRaises(Exception):
            cert = self._get_cert(addr, ciphers=CIPHER_MAP['ecdsa'])
            self.assertEqual(cert.get_subject().commonName.decode(), 'www.example.com')

    def test_intermediate_ca_rsa(self):
        self._intermediate_ca_t('rsa')

    def test_intermediate_ca_ecdsa(self):
        with self.assertRaises(Exception):
            self._intermediate_ca_t('ecdsa')


class TestECDSA(helpers.EnvironmentCase, CertSelectionMixin):
    '''
    Tests for https for ATS configured with ECDSA certificates
    '''
    @classmethod
    def setUpEnv(cls, env):
        # add an SSL port to ATS
        cls.ssl_port = tsqa.utils.bind_unused_port()[1]
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:ssl'.format(cls.ssl_port)
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'ssl',
            'proxy.config.ssl.server.cipher_suite': CIPHER_MAP['ecdsa'],
        })

        # configure SSL multicert
        cls.configs['ssl_multicert.config'].add_line('dest_ip=127.0.0.2 ssl_cert_name={0} ssl_ca_name={1}'.format(
            helpers.tests_file_path('ec_keys/www.example.com.pem'),
            helpers.tests_file_path('ec_keys/intermediate.crt'),
        ))
        cls.configs['ssl_multicert.config'].add_line('dest_ip=127.0.0.2 ssl_cert_name={0}'.format(
            helpers.tests_file_path('ec_keys/www.test.com.pem'),
        ))

        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0} ssl_ca_name={1}'.format(
            helpers.tests_file_path('ec_keys/www.example.com.pem'),
            helpers.tests_file_path('ec_keys/intermediate.crt'),
        ))
        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0}'.format(
            helpers.tests_file_path('ec_keys/www.test.com.pem'),
        ))

    def test_rsa(self):
        addr = ('127.0.0.1', self.ssl_port)
        with self.assertRaises(Exception):
            cert = self._get_cert(addr, ciphers=CIPHER_MAP['rsa'])
            self.assertEqual(cert.get_subject().commonName.decode(), 'www.example.com')

    def test_ecdsa(self):
        addr = ('127.0.0.1', self.ssl_port)
        cert = self._get_cert(addr, ciphers=CIPHER_MAP['ecdsa'])
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.example.com')

    def test_intermediate_ca_rsa(self):
        with self.assertRaises(Exception):
            self._intermediate_ca_t('rsa')

    def test_intermediate_ca_ecdsa(self):
        self._intermediate_ca_t('ecdsa')


class TestMix(helpers.EnvironmentCase, CertSelectionMixin):
    '''
    Tests for https for ATS configured with both ECDSA and RSA certificates
    '''
    @classmethod
    def setUpEnv(cls, env):
        # Temporarily skipping TestMix until we can figure out how to specify underlying open ssl versions
        # The behaviour of the intermediate cert chains depends on openssl version
        raise helpers.unittest.SkipTest('Skip TestMix until we figure out openssl version tracking');
        # add an SSL port to ATS
        cls.ssl_port = tsqa.utils.bind_unused_port()[1]
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:ssl'.format(cls.ssl_port)
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'ssl',
            'proxy.config.ssl.server.cipher_suite': '{0}:{1}'.format(CIPHER_MAP['ecdsa'], CIPHER_MAP['rsa']),
        })

        # configure SSL multicert
        cls.configs['ssl_multicert.config'].add_line('dest_ip=127.0.0.2 ssl_cert_name={0},{1} ssl_ca_name={2},{3}'.format(
            helpers.tests_file_path('rsa_keys/www.example.com.pem'),
            helpers.tests_file_path('ec_keys/www.example.com.pem'),
            helpers.tests_file_path('rsa_keys/intermediate.crt'),
            helpers.tests_file_path('ec_keys/intermediate.crt'),
        ))
        cls.configs['ssl_multicert.config'].add_line('dest_ip=127.0.0.2 ssl_cert_name={0},{1}'.format(
            helpers.tests_file_path('rsa_keys/www.test.com.pem'),
            helpers.tests_file_path('ec_keys/www.test.com.pem'),
        ))

        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0},{1} ssl_ca_name={2},{3}'.format(
            helpers.tests_file_path('rsa_keys/www.example.com.pem'),
            helpers.tests_file_path('ec_keys/www.example.com.pem'),
            helpers.tests_file_path('rsa_keys/intermediate.crt'),
            helpers.tests_file_path('ec_keys/intermediate.crt'),
        ))
        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0},{1}'.format(
            helpers.tests_file_path('rsa_keys/www.test.com.pem'),
            helpers.tests_file_path('ec_keys/www.test.com.pem'),
        ))

    def test_rsa(self):
        addr = ('127.0.0.1', self.ssl_port)
        cert = self._get_cert(addr, ciphers=CIPHER_MAP['rsa'])
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.example.com')

    def test_ecdsa(self):
        addr = ('127.0.0.1', self.ssl_port)
        cert = self._get_cert(addr, ciphers=CIPHER_MAP['ecdsa'])
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.example.com')

    def test_intermediate_ca_rsa(self):
        self._intermediate_ca_t('rsa')

    def test_intermediate_ca_ecdsa(self):
        self._intermediate_ca_t('ecdsa')


class TestConfigFileGroup(helpers.EnvironmentCase, CertSelectionMixin):
    '''
    Tests for config file group with https
    The config file group includes a parent file ssl_multicert.config and some children files.
    when the content of a child file is updated but the file name hasn't been changed.
    The behavior is the same as the parent file in the group has been changed.
    In the test, a child file named www.unknown.com.pem, which is rsa_keys/www.test.com.pem at first,
      is updated to ec_keys/www.test.com.pem.
    The difference can be told by different results from calling get_cert() with different ciphers as paramters
    '''
    @classmethod
    def setUpEnv(cls, env):
        # add an SSL port to ATS
        cls.ssl_port = tsqa.utils.bind_unused_port()[1]
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:ssl'.format(cls.ssl_port)
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'ssl',
            'proxy.config.ssl.server.cipher_suite': '{0}:{1}'.format(CIPHER_MAP['ecdsa'], CIPHER_MAP['rsa']),
        })
        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0},{1} ssl_ca_name={2},{3}'.format(
            helpers.tests_file_path('rsa_keys/www.example.com.pem'),
            helpers.tests_file_path('ec_keys/www.example.com.pem'),
            helpers.tests_file_path('rsa_keys/intermediate.crt'),
            helpers.tests_file_path('ec_keys/intermediate.crt'),
        ))
        cls.configs['ssl_multicert.config'].add_line('dest_ip=127.0.0.3 ssl_cert_name={0}'.format(
            helpers.tests_file_path('www.unknown.com.pem'),
        ))
        os.system('cp %s %s' % (helpers.tests_file_path('rsa_keys/www.test.com.pem'), helpers.tests_file_path('www.unknown.com.pem')))
        log.info('cp %s %s' % (helpers.tests_file_path('rsa_keys/www.test.com.pem'), helpers.tests_file_path('www.unknown.com.pem')))

    def test_config_file_group(self):
        signal_cmd = os.path.join(self.environment.layout.bindir, 'traffic_line') + ' -x'
        addr = ('127.0.0.3', self.ssl_port)
        cert = self._get_cert(addr, ciphers=CIPHER_MAP['rsa'])
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.test.com')
        with self.assertRaises(Exception):
          self._get_cert(addr, ciphers=CIPHER_MAP['ecdsa'])
        time.sleep(5)
        os.system('cp %s %s' % (helpers.tests_file_path('ec_keys/www.test.com.pem'), helpers.tests_file_path('www.unknown.com.pem')))
        log.info('cp %s %s' % (helpers.tests_file_path('ec_keys/www.test.com.pem'), helpers.tests_file_path('www.unknown.com.pem')))
        os.system(signal_cmd)
        log.info(signal_cmd)
        # waiting for the reconfiguration completed
        sec = 0
        while True:
          time.sleep(5)
          sec += 5
          log.info("reloading: %d seconds" % (sec))
          self.assertLess(sec, 30)
          try:
            self._get_cert(addr, ciphers=CIPHER_MAP['ecdsa'])
            break
          except:
            continue
        cert = self._get_cert(addr, ciphers=CIPHER_MAP['ecdsa'])
        self.assertEqual(cert.get_subject().commonName.decode(), 'www.test.com')
        with self.assertRaises(Exception):
          self._get_cert(addr, ciphers=CIPHER_MAP['rsa'])
        os.system('rm %s' %(helpers.tests_file_path('www.unknown.com.pem')))
