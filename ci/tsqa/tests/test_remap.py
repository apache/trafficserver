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
import logging

import helpers

import tsqa.test_cases
import tsqa.utils
import tsqa.endpoint

log = logging.getLogger(__name__)


class TestRemapHTTP(tsqa.test_cases.DynamicHTTPEndpointCase, helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'url.*',
        })

        cls.configs['remap.config'].add_line(
            'map http://www.example.com http://127.0.0.1:{0}'.format(cls.http_endpoint.address[1])
        )
        cls.configs['remap.config'].add_line(
            'map http://www.example.com:8080 http://127.0.0.1:{0}'.format(cls.http_endpoint.address[1])
        )

        def hello(request):
            return 'hello'
        cls.http_endpoint.add_handler('/', hello)

    def test_remap_http(self):
        s = requests.Session()
        http_port = self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']
        url = 'http://127.0.0.1:{0}/'.format(http_port)

        ret = s.get(url)
        self.assertEqual(ret.status_code, 404)

        s.headers.update({'Host': 'www.example.com'})
        ret = s.get(url)
        self.assertEqual(ret.status_code, 200)

        s.headers.update({'Host': 'www.example.com:80'})
        ret = s.get(url)
        self.assertEqual(ret.status_code, 200)

        s.headers.update({'Host': 'www.example.com:8080'})
        ret = s.get(url)
        self.assertEqual(ret.status_code, 200)

        s.headers.update({'Host': 'www.test.com'})
        ret = s.get(url)
        self.assertEqual(ret.status_code, 404)

        s.headers.update({'Host': 'www.example.com:1234'})
        ret = s.get(url)
        self.assertEqual(ret.status_code, 404)


class TestRemapHTTPS(tsqa.test_cases.DynamicHTTPEndpointCase, helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        # set an SSL port to ATS
        cls.ssl_port = tsqa.utils.bind_unused_port()[1]
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:ssl'.format(cls.ssl_port)
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'url.*'
        })

        cls.configs['remap.config'].add_line(
            'map https://www.example.com http://127.0.0.1:{0}'.format(cls.http_endpoint.address[1])
        )
        cls.configs['remap.config'].add_line(
            'map https://www.example.com:4443 http://127.0.0.1:{0}'.format(cls.http_endpoint.address[1])
        )
        # configure SSL multicert
        cls.configs['ssl_multicert.config'].add_line(
            'dest_ip=* ssl_cert_name={0}'.format(helpers.tests_file_path('rsa_keys/www.example.com.pem'))
        )

        def hello(request):
            return 'hello'
        cls.http_endpoint.add_handler('/', hello)

    def test_remap_https(self):
        s = requests.Session()
        url = 'https://127.0.0.1:{0}/'.format(self.ssl_port)

        # We lack of SNI support in requests module, so we do not verify SSL certificate here.
        # ret = s.get(url, verify=(helpers.tests_file_path('certs/ca.crt')))
        ret = s.get(url, verify=False)
        self.assertEqual(ret.status_code, 404)

        s.headers.update({'Host': 'www.example.com'})
        ret = s.get(url, verify=False)
        self.assertEqual(ret.status_code, 200)

        s.headers.update({'Host': 'www.example.com:443'})
        ret = s.get(url)
        self.assertEqual(ret.status_code, 200)

        s.headers.update({'Host': 'www.example.com:4443'})
        ret = s.get(url)
        self.assertEqual(ret.status_code, 200)

        s.headers.update({'Host': 'www.test.com'})
        ret = s.get(url)
        self.assertEqual(ret.status_code, 404)

        s.headers.update({'Host': 'www.example.com:1234'})
        ret = s.get(url)
        self.assertEqual(ret.status_code, 404)
