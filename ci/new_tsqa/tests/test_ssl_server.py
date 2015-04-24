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
import requests

import helpers
import tsqa.utils
import tsqa.test_cases
import tsqa.endpoint

unittest = tsqa.utils.import_unittest()

class TestSSLServer(helpers.EnvironmentCase, tsqa.test_cases.HTTPBinCase):
    '''
    Tests for SSL between client and ATS (as server)
    '''
    @classmethod
    def setUpEnv(cls, env):
        # add an SSL port to ATS
        cls.ssl_port = tsqa.utils.bind_unused_port()[1]
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:ssl'.format(cls.ssl_port)
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'ssl',
        })

        # configure remap
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}'.format(cls.http_endpoint.address[1]))

        # configure SSL multicert
        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0} ssl_ca_name={1}'.format(
            helpers.tests_file_path('rsa_keys2/www.example.com.pem'),
            helpers.tests_file_path('rsa_keys2/chain.crt'),
            ))

    def test_ssl_termination(self):
        r = requests.get('https://www.example.com:{0}/get'.format(self.ssl_port),
                verify=helpers.tests_file_path('rsa_keys2/ca.crt'))
        self.assertEqual(r.status_code, 200)

if __name__ == '__main__':
    unittest.main()
