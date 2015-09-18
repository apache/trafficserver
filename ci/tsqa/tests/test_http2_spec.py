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

#
# Note: This test case uses h2spec. Please install it yourself.
# https://github.com/summerwind/h2spec
#

import logging
import os
import subprocess

import helpers

import tsqa.endpoint
import tsqa.test_cases
import tsqa.utils

log = logging.getLogger(__name__)


# helper function to get h2spec path
def which(program):
    def is_exe(fpath):
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)
    fpath, fname = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            path = path.strip('"')
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file
    return None


class TestH2Spec(helpers.EnvironmentCase, tsqa.test_cases.HTTPBinCase):
    @classmethod
    def setUpEnv(cls, env):
        '''
        Setting up environment for testing of HTTP2
        '''
        # get path to h2spec
        cls.h2spec = which('h2spec')
        if cls.h2spec is None:
          raise helpers.unittest.SkipTest('Cannot find h2spec. skipping test.')

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

    def __callH2Spec(self, section=None):
        '''
        Call h2spec
        '''
        args = [self.h2spec, '-h', 'localhost', '-p', str(self.http2_port), '-t', '-k']
        if section is not None:
          args.extend(['-s', section])

        log.info('full args = {0}'.format(args))
        p = subprocess.Popen(
            args,
            stdout=subprocess.PIPE,
            stdin=subprocess.PIPE,
        )
        self.stdout, self.stderr = p.communicate()
        log.info('\n' + self.stdout)

        return p.returncode

    def test_http2_spec_section(self):
        '''
        Test HTTP/2 w/ h2spec (Exceptional Scenario)
        '''
        sections = ['3.5', '4.2', '5.1', '5.3.1', '5.4.1', '5.5', '6.1', '6.2', '6.3', '6.4', '6.5', '6.5.2', '6.7', '6.8',
                    '6.9', '6.9.1', '6.10', '8.1', '8.1.2', '8.1.2.2', '8.1.2.3', '8.1.2.6', '8.2']
        for section in sections:
            self.__callH2Spec(section)
            self.assertIn('All tests passed', self.stdout, 'Failed at section %s of RFC7540' % section)

        # TODO these tests cannot pass currently. move to above after ATS can pass them
        failing_sections = ['4.3']
        for section in failing_sections:
            self.__callH2Spec(section)
            self.assertNotIn('All tests passed', self.stdout, 'Failed at section %s of RFC7540' % section)
