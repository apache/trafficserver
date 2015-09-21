'''
Some example tests of the new tsqa
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
import subprocess

import helpers

import tsqa.test_cases
import tsqa.utils

# TODO: for some reason subclasses of subclasses of TestCase don't work with the
# decorator
# @helpers.unittest.skip('Not running TestNoOp, as it is a NoOp test')


class TestNoOp(helpers.EnvironmentCase):
    '''
    This is purely a documentation test
    '''
    # you can set configure/environment options for the source build here
    environment_factory = {
        'configure': {
            'enable-spdy': None,  # A value of None means that the argument has no value
            'with-max-api-stats': 2048,  # if there is a value it will be converted to --key=value
        },
        'env': None,
    }

    @classmethod
    def setUpClass(cls):
        '''
        If you'd like to skip an entire test
        '''
        # you can also skip (or conditionally skip) tests
        raise helpers.unittest.SkipTest('Skip the entire class')

    @classmethod
    def setUpEnv(cls, env):
        '''
        This funciton is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start.

        You are passed in cls (which is the instance of this class) and env (which
        is an environment object)
        '''
        # we can modify any/all configs (note: all pre-daemon start)
        cls.configs['remap.config'].add_line('map / http://http://trafficserver.readthedocs.org/')

        # Some configs have nicer wrapper objects to give you a more pythonic interface
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.log.squid_log_enabled': 1,
            'proxy.config.log.squid_log_is_ascii': 1,
        })

    def test_something(self):
        '''
        All functions beginning with "test_" will be run as tests for the class.
        Within these functions your environment is already set up and started--
        you only need to excercise the code that you intend to test
        '''
        # for example, you could send a request to ATS and check the response
        ret = requests.get('http://127.0.0.1:{0}/'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']))

        self.assertEqual(ret.status_code, 404)
        self.assertIn('ATS', ret.headers['server'])


class TestConfigureFlags(helpers.EnvironmentCase):
    environment_factory = {
        'configure': {'enable-spdy': None},
    }

    def test_spdy(self):
        self.assertTrue(True)


class TestBootstrap(helpers.EnvironmentCase):
    def test_default_404(self):
        ret = requests.get('http://127.0.0.1:{0}/'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']))

        self.assertEqual(ret.status_code, 404)
        self.assertIn('ATS', ret.headers['server'])

    def test_trafficline(self):
        '''
        Test that traffic_line works, and verify that the values for proxy.config
        match what we put in records.config
        '''
        cmd = [os.path.join(self.environment.layout.bindir, 'traffic_line'),
               '-m',
               'proxy.config',
               ]
        stdout, _ = tsqa.utils.run_sync_command(cmd, stdout=subprocess.PIPE)
        for line in stdout.splitlines():
            if not line.strip():
                continue
            k, v = line.split(' ', 1)
            if k not in self.configs['records.config']['CONFIG']:
                continue
            r_val = self.configs['records.config']['CONFIG'][k]
            self.assertEqual(type(r_val)(v), self.configs['records.config']['CONFIG'][k])


class TestServerIntercept(helpers.EnvironmentCase, tsqa.test_cases.DynamicHTTPEndpointCase):
    endpoint_port = 60000

    @classmethod
    def setUpEnv(cls, env):
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}'.format(cls.endpoint_port))

        cls.configs['plugin.config'].add_line('intercept.so')

        def hello(request):
            return 'hello'
        cls.http_endpoint.add_handler('/', hello)

    def test_basic_intercept(self):
        for _ in xrange(0, 10):
            ret = requests.get('http://127.0.0.1:{0}/'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']))

            self.assertEqual(ret.status_code, 200)


class TestLogs(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        '''
        This funciton is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''
        # only add server headers when there weren't any
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.diags.debug.tags': 'log-.*',
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.log.hostname': 'test',
        })

    def test_logs_exist(self):
        # send some requests
        for x in xrange(0, 10):
            ret = requests.get('http://127.0.0.1:{0}/'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']))

            self.assertEqual(ret.status_code, 404)
            self.assertIn('ATS', ret.headers['server'])

        # TODO: some better way to know when the logs where syncd
        time.sleep(10)  # wait for logs to hit disk

        # verify that the log files exist
        for logfile in ('diags.log', 'error.log', 'squid.blog', 'traffic.out', 'manager.log'):
            logfile_path = os.path.join(self.environment.layout.logdir, logfile)
            self.assertTrue(os.path.isfile(logfile_path), logfile_path)


class TestDynamicHTTPEndpointCase(tsqa.test_cases.DynamicHTTPEndpointCase, helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        '''
        This funciton is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}/\n'.format(cls.http_endpoint.address[1]))

        # only add server headers when there weren't any
        cls.configs['records.config']['CONFIG']['proxy.config.http.response_server_enabled'] = 2

    def test_basic_proxy(self):
        ret = requests.get(self.endpoint_url('/test'),
                           proxies=self.proxies,
                           )
        self.assertEqual(ret.status_code, 404)
        self.assertIn('WSGIServer', ret.headers['server'])
