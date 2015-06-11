'''
Test that configuration options successfully compile
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

import logging
import helpers

log = logging.getLogger(__name__)


class TestBuildOption(helpers.EnvironmentCase):
    '''
    Run the built-in traffic_server regression test suite.
    '''
    def test_buildoption(self):
        pass


class TestBuildOptionFastSDK(TestBuildOption):
    '''Build with --enable-fast-sdk'''
    environment_factory = {'configure': {'enable-fast-sdk': None}}


class TestBuildOptionDisableDiags(TestBuildOption):
    '''Build with --disable-diags'''
    environment_factory = {'configure': {'disable-diags': None}}


class TestBuildOptionDisableTests(TestBuildOption):
    '''Build with --disable-tests'''
    environment_factory = {'configure': {'disable-tests': None}}


class TestBuildOptionEnableStaticProxy(TestBuildOption):
    '''Build with --enable-static-proxy'''
    environment_factory = {'configure': {'enable-static-proxy': None}}

    @classmethod
    def setUpClass(cls):
        raise helpers.unittest.SkipTest('Skip until TS-3577 is resolved')


class TestBuildOptionEnableCxxApi(TestBuildOption):
    '''Build with --enable-cppapi'''
    environment_factory = {'configure': {'enable-cppapi': None}}

    @classmethod
    def setUpClass(cls):
        raise helpers.unittest.SkipTest('Skip until atscppapi supports out of tree builds')
