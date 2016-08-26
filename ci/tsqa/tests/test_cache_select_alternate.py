"""Test the select alternate configuration."""

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
import subprocess
import uuid

import helpers

import requests

import tsqa.test_cases
import tsqa.utils

log = logging.getLogger(__name__)


class TestSelectAlternate(helpers.EnvironmentCase,
                          tsqa.test_cases.DynamicHTTPEndpointCase):
    """Test the select alternate config."""

    def _fetch(self, path, ua='user-agent1'):
        url = 'http://127.0.0.1:{}/{}'.format(
            self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'],
            path
        )
        log.debug('get {}'.format(url))
        return requests.get(url, headers={
            'user-agent': ua,
            'x-debug': 'x-cache'
        })

    def _dump(self, response):
        log.info('HTTP response {}'.format(response.status_code))
        for k, v in response.headers.items():
            log.info('    {}: {}'.format(k, v))

    def _ctl(self, *args):
        cmd = [os.path.join(self.environment.layout.bindir, 'traffic_ctl')] + list(args)
        out, _ = tsqa.utils.run_sync_command(
            cmd,
            env=self.environment.shell_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        return out

    @classmethod
    def setUpEnv(cls, env):
        """Set up environment."""
        cls.config_file = 'header-rewrite-for-select-alternate.config'
        cls.test_config_path = helpers.tests_file_path(cls.config_file)

        cls.configs['plugin.config'].add_line('%s/header_rewrite.so %s' % (
            cls.environment.layout.plugindir,
            cls.test_config_path
        ))
        cls.configs['plugin.config'].add_line('xdebug.so')

        cls.configs['remap.config'].add_line(
            'map /default/ http://127.0.0.1/ @plugin=generator.so'
        )


class TestSelectAlternateEnabled(TestSelectAlternate):
    """Test the enabled select alternate config."""

    @classmethod
    def setUpEnv(cls, env):
        """Set up environment."""
        super(TestSelectAlternateEnabled, cls).setUpEnv(env)

        # Start with select alternate turned on
        cls.configs['records.config']['CONFIG']['proxy.config.cache.select_alternate'] = 1

    def test_select_alternate_enabled(self):
        """Test that select alternate generate cache per user agent."""
        objectid = uuid.uuid4()

        # First touch is a MISS.
        ret = self._fetch('default/cache/10/{}'.format(objectid))
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'miss')

        # Second touch is a HIT.
        ret = self._fetch('default/cache/10/{}'.format(objectid))
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'hit-fresh')

        # A touch with another User Agent is a MISS.
        ret = self._fetch('default/cache/10/{}'.format(objectid), ua='user-agent2')
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'miss')


class TestSelectAlternateDisabled(TestSelectAlternate):
    """Test the disabled select alternate config."""

    @classmethod
    def setUpEnv(cls, env):
        """Set up environment."""
        super(TestSelectAlternateDisabled, cls).setUpEnv(env)

        # Start with select alternate turned on
        cls.configs['records.config']['CONFIG']['proxy.config.cache.select_alternate'] = 0

    def test_select_alternate_disabled(self):
        """Test that disabled select alternate uses same cache for all user agents."""
        objectid = uuid.uuid4()

        # First touch is a MISS.
        ret = self._fetch('default/cache/10/{}'.format(objectid))
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'miss')

        # Second touch is a HIT.
        ret = self._fetch('default/cache/10/{}'.format(objectid))
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'hit-fresh')

        # A touch with another User Agent is a HIT.
        ret = self._fetch('default/cache/10/{}'.format(objectid), ua='user-agent2')
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'hit-fresh')
