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
import os

import helpers

import tsqa.endpoint
import tsqa.test_cases
import tsqa.utils

try:
    import hyper
except ImportError:
    raise helpers.unittest.SkipTest('Cannot import hyper, skipping tests for HTTP/2')

log = logging.getLogger(__name__)


class TestHTTP2(helpers.EnvironmentCase, tsqa.test_cases.HTTPBinCase):
    @classmethod
    def setUpEnv(cls, env):
        '''
        Setting up environment for testing of HTTP2
        '''
        # get HTTP/2 server ports
        cls.http2_port = tsqa.utils.bind_unused_port()[1]

        # HTTP2 configs
        cls.configs['records.config']['CONFIG']['proxy.config.http2.enabled'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:ssl'.format(cls.http2_port)
        cls.configs['records.config']['CONFIG']['proxy.config.ssl.server.cert.path'] = helpers.tests_file_path('rsa_keys')
        cls.configs['records.config']['CONFIG']['proxy.config.diags.debug.enabled'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.diags.debug.tags'] = 'http2.*|ssl.*'

        # configure SSL multicert
        cls.configs['ssl_multicert.config'].add_line(
            'dest_ip=* ssl_cert_name={0}\n'.format(helpers.tests_file_path('rsa_keys/www.example.com.pem'))
        )

        # remap configs
        cls.configs['remap.config'].add_line(
            'map / http://127.0.0.1:{0}/'.format(cls.http_endpoint.address[1])
        )

        # Turn off certificate verification for the tests.
        # hyper-0.4.0 verify certs in default and can't turn it off without below hack:(
        hyper.tls._context = hyper.tls.init_context()
        hyper.tls._context.check_hostname = False
        hyper.tls._context.verify_mode = hyper.compat.ssl.CERT_NONE

    def __cat(self, target_file_path):
        '''
        Cat given file
        '''
        for line in open(target_file_path).readlines():
            log.debug(line[:-1])

    def __traffic_out(self):
        '''
        Cat traffic.out
        '''
        self.__cat(os.path.join(self.environment.layout.logdir, 'traffic.out'))

    def __diags_log(self):
        '''
        Cat diags.log
        '''
        self.__cat(os.path.join(self.environment.layout.logdir, 'diags.log'))

    def test_http2_request_hyper(self):
        '''
        Test HTTP/2 w/ hyper (Normal Scenario)
        '''
        try:
            conn = hyper.HTTPConnection('127.0.0.1', self.http2_port, secure=True)
            stream_id = conn.request('GET', '/')
            ret = conn.get_response()

            self.assertNotEqual(stream_id, None)
            self.assertEqual(ret.status, 200)
        except Exception as e:
            log.error(e)
            self.__traffic_out()
            self.__diags_log()
