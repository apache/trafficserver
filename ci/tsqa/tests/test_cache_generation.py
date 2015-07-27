'''
Test the cache generation configuration
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
import subprocess
import logging
import requests
import random
import uuid
import time

import helpers
import tsqa.test_cases
import tsqa.utils

log = logging.getLogger(__name__)


class TestCacheGeneration(helpers.EnvironmentCase):
    '''
    Test the cache object generation ID.
    '''

    def _fetch(self, path):
        url = 'http://127.0.0.1:{}/{}'.format(
            self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'],
            path
        )
        log.debug('get {}'.format(url))
        return requests.get(url, headers={'x-debug': 'x-cache,x-cache-key,via,x-cache-generation'})

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

        cls.configs['plugin.config'].add_line('xdebug.so')

        cls.configs['remap.config'].add_line(
            'map /default/ http://127.0.0.1/ @plugin=generator.so'
        )
        cls.configs['remap.config'].add_line(
            'map /generation1/ http://127.0.0.1/' +
            ' @plugin=conf_remap.so @pparam=proxy.config.http.cache.generation=1' +
            ' @plugin=generator.so'
        )
        cls.configs['remap.config'].add_line(
            'map /generation2/ http://127.0.0.1/' +
            ' @plugin=conf_remap.so @pparam=proxy.config.http.cache.generation=2' +
            ' @plugin=generator.so'
        )

        # Start with cache generation turned off
        cls.configs['records.config']['CONFIG']['proxy.config.http.cache.generation'] = -1
        # Wait for the cache so we don't race client requests against it.
        cls.configs['records.config']['CONFIG']['proxy.config.http.wait_for_cache'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.config_update_interval_ms'] = 1

    def test_generations_are_disjoint(self):
        """Test that the same URL path in different cache generations creates disjoint objects"""
        objectid = uuid.uuid4()

        # First touch is a MISS.
        ret = self._fetch('default/cache/10/{}'.format(objectid))
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'miss', msg=ret)
        self.assertEqual(ret.headers['x-cache-generation'], '-1')

        # Same URL in generation 1 is a MISS.
        ret = self._fetch('generation1/cache/10/{}'.format(objectid))
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'miss')
        self.assertEqual(ret.headers['x-cache-generation'], '1')

        # Same URL in generation 2 is still a MISS.
        ret = self._fetch('generation2/cache/10/{}'.format(objectid))
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'miss')
        self.assertEqual(ret.headers['x-cache-generation'], '2')

        # Second touch is a HIT.
        ret = self._fetch('default/cache/10/{}'.format(objectid))
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'hit-fresh', msg=ret.headers['x-cache'])
        self.assertEqual(ret.headers['x-cache-generation'], '-1')

    def test_online_cache_clear(self):
        """Test that incrementing the cache generation acts like a cache clear"""
        objectid = uuid.uuid4()

        # First touch is a MISS.
        ret = self._fetch('default/cache/10/{}'.format(objectid))
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'miss')

        # Second touch is a HIT.
        ret = self._fetch('default/cache/10/{}'.format(objectid))
        self.assertEqual(ret.status_code, 200)
        self.assertEqual(ret.headers['x-cache'], 'hit-fresh')

        # Now update the generation number.
        timeout = float(self._ctl('config', 'get', 'proxy.config.config_update_interval_ms').split(' ')[-1])
        generation = random.randrange(65000)
        gencount = 0

        self._ctl('config', 'set', 'proxy.config.http.cache.generation', str(generation))
        self._ctl('config', 'reload')

        for _ in xrange(5):
            if gencount == 0:
                log.debug('waiting {} secs for the config to update'.format(timeout / 1000))
                time.sleep(timeout / 1000)

            ret = self._fetch('default/cache/10/{}'.format(objectid))
            self.assertEqual(ret.status_code, 200)

            if ret.headers['x-cache-generation'] == str(generation):
                if gencount == 0:
                    # First time we see the new generation, it should be a miss.
                    self.assertEqual(ret.headers['x-cache'], 'miss')
                else:
                    # Now the previous hits should become misses.
                    self.assertEqual(ret.headers['x-cache'], 'hit-fresh')
            else:
                # Config has not updated, so it should be a hit.
                self.assertEqual(ret.headers['x-cache'], 'hit-fresh')
                self.assertEqual(ret.headers['x-cache-generation'], '-1')

                gencount = gencount + 1

        self.assertNotEqual(gencount, 0, msg='proxy.config.http.cache.generation never updated')
