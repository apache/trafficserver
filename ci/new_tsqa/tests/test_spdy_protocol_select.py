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
import subprocess

import helpers

import tsqa.test_cases
import tsqa.utils
import tsqa.endpoint

log = logging.getLogger(__name__)

class TestSPDY(helpers.EnvironmentCase):
    environment_factory = {
        'configure': {'enable-spdy': None},
        'env': {'PKG_CONFIG_PATH': '/opt/spdylay/lib/pkgconfig/'},
        }

    @classmethod
    def setUpEnv(cls, env):
        '''
        This function is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''

        # set up spdycat
        build_dir = os.environ.get('top_builddir', '../..')
        cls.client = '%s/spdylay/src/spdycat' % build_dir
        log.info('top build_dir = {0}'.format(build_dir))
        log.info('spdycat path = {0}'.format(cls.client))

        # get spdy server ports
        cls.spdy_port = tsqa.utils.bind_unused_port()[1]
        log.info('spdy server port = {0}'.format(cls.spdy_port))
        cls.http_port = tsqa.utils.bind_unused_port()[1]
        log.info('http server port = {0}'.format(cls.http_port))

        cls.configs['remap.config'].add_line('map / http://www.yahoo.com/\n')
        
        # set only one ET_NET thread (so we don't have to worry about the per-thread pools causing issues)
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.limit'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.exec_thread.autoconfig'] = 0
 
        # SPDY configs
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:ssl {1}:proto=http:ssl'.format(cls.spdy_port, cls.http_port)
        cls.configs['records.config']['CONFIG']['proxy.config.ssl.server.cert.path'] = helpers.tests_file_path('rsa_keys')
        
        # optional enable debug logs
        #cls.configs['records.config']['CONFIG']['proxy.config.diags.debug.enabled'] = 1
        #cls.configs['records.config']['CONFIG']['proxy.config.diags.debug.tags'] = 'http.*|dns.*|ssl'

        # configure SSL multicert
        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0}\n'.format(helpers.tests_file_path('rsa_keys/localhost.pem')))

    @classmethod
    def callSpdycat(self, port, path, args):
      full_args = [self.client,'https://localhost:%d%s' % (port, path)] + args
      self.log.info('full args = {0}'.format(full_args))
      p = subprocess.Popen(full_args, stdout=subprocess.PIPE,
          stdin=subprocess.PIPE)
      self.stdout, self.stderr = p.communicate()
      return p.returncode

"""
TODO: re-add spdy2 tests. looks like support here might be lacking some way. was not able to get ATS to advertise spdy/2
even when it was explicitly set with proto=spdy/2
"""
class TestSPDYv2(TestSPDY):
    @classmethod
    def setUpClass(cls):
        '''
        Skip spdy2 tests for now
        '''
        raise helpers.unittest.SkipTest('Skipping spdy/2 tests')
    
    @classmethod
    def setUpEnv(cls, env):
        '''
        This function is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''
        
        cls.spdy2_port = tsqa.utils.bind_unused_port()[1]
        log.info('spdy2 server port = {0}'.format(cls.spdy2_port))
        # make sure we add port supports spdy2
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:proto=spdy/2:ssl'.format(cls.spdy2_port)

        super(TestSPDYv2, cls).setUpEnv(env)
    
    def test_SPDY_v2(self):
        '''
        Test that the origin does in fact support spdy 2
        '''
        self.assertEquals(0, self.callSpdycat(self.spdy2_port, '/', ['-nv', '--spdy2']))
        self.assertIn('NPN selected the protocol: spdy/2', self.stdout)

class TestSPDYv3(TestSPDY):
    def test_SPDY_v3(self):
        '''
        Test that the origin does in fact support spdy 3
        '''
        self.assertEquals(0, self.callSpdycat(self.spdy_port, '/', ['-nv', '--spdy3']))
        self.assertIn('NPN selected the protocol: spdy/3', self.stdout)

    def test_SPDY_v3_failed_request(self):
        '''
        Test that non spdy port won't advertise spdy
        '''
        self.assertEquals(1, self.callSpdycat(self.http_port, '/', ['-nv', '--spdy3']))

class TestSPDYv3_1(TestSPDY):
    def test_SPDY_v3_1(self):
        '''
        Test that the origin does in fact support spdy 3.1
        '''
        self.assertEquals(0, self.callSpdycat(self.spdy_port, '/', ['-nv', '--spdy3-1']))
        self.assertIn('NPN selected the protocol: spdy/3.1', self.stdout)

    def test_SPDY_v3_1_failed_request(self):
        '''
        Test that non spdy port won't advertise spdy
        '''
        self.assertEquals(1, self.callSpdycat(self.http_port, '/', ['-nv', '--spdy3-1']))
