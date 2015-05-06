'''
Run the built-in regression tests with experimental build configurations.
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

import helpers
import tsqa.test_cases
import tsqa.utils

log = logging.getLogger(__name__)


class TestRegressions(helpers.EnvironmentCase):
    '''
    Run the built-in traffic_server regression test suite.
    '''

    # NOTE: we need to stop the running Traffic Server in the environment so
    # that we can start up our own. Make sure to restart it when we are done so
    # that the EnvironmentCase doesn't get upset.

    @classmethod
    def setUpClass(cls):
        super(TestRegressions, cls).setUpClass()
        cls.environment.stop()

    @classmethod
    def tearDownClass(cls):
        cls.environment.start()
        super(TestRegressions, cls).tearDownClass()

    def test_regressions(self):
        cmd = [os.path.join(self.environment.layout.bindir, 'traffic_server'), '-R', '1']
        tsqa.utils.run_sync_command(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


class TestRegressionsLinuxNativeAIO(TestRegressions):
    '''
    Run the built-in traffic_server regression test suite with
    --enable-linux-native-aio.
    '''
    environment_factory = {
        'configure': {'enable-linux-native-aio': None},
    }
