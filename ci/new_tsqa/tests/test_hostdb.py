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

import helpers

import tsqa.test_cases


class TestHostDBFailedDNS(tsqa.test_cases.DynamicHTTPEndpointCase, helpers.EnvironmentCase):
    '''
    Tests for how hostdb handles when there is no reachable resolver
    '''
    @classmethod
    def setUpEnv(cls, env):
        cls.configs['remap.config'].add_line('map / http://some_nonexistant_domain:{0}/'.format(cls.http_endpoint.address[1]))

        resolv_conf_path = os.path.join(env.layout.prefix, 'resolv.conf')

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.response_server_enabled': 2,  # only add server headers when there weren't any
            'proxy.config.hostdb.lookup_timeout': 1,
            'proxy.config.dns.resolv_conf': resolv_conf_path,

        })

        with open(resolv_conf_path, 'w') as fh:
            fh.write('nameserver 1.1.1.1\n')  # some non-existant nameserver

    def test_lookup_timeout(self):
        start = time.time()
        ret = requests.get(self.endpoint_url('/test'),
                           proxies=self.proxies,
                           )
        self.assertGreater(time.time() - start, self.configs['records.config']['CONFIG']['proxy.config.hostdb.lookup_timeout'])
        self.assertEqual(ret.status_code, 502)
        self.assertIn('ATS', ret.headers['server'])

