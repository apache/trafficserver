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
import helpers
import tsqa.test_cases
import tsqa.utils
import tsqa.endpoint


class TestRedirection(helpers.EnvironmentCase, tsqa.test_cases.HTTPBinCase):
    @classmethod
    def setUpEnv(cls, env):
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.redirection_enabled': 1,
            'proxy.config.http.number_of_redirections': 10
        })
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}'.format(cls.http_endpoint.address[1]))

    def test_redirection(self):
        server_ports = self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']

        # By default Requests will perform location redirection
        # Disable redirection handling with the allow_redirects parameter
        r = requests.get('http://127.0.0.1:{0}/redirect/9'.format(server_ports), allow_redirects=False)
        self.assertEqual(r.status_code, 200)

        r = requests.get('http://127.0.0.1:{0}/redirect/10'.format(server_ports), allow_redirects=False)
        self.assertEqual(r.status_code, 302)
