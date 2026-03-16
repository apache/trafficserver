'''
Tests that the Content-Type directive in .body_factory_info is honored
for body factory error responses.
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

Test.Summary = 'Verify Content-Type directive in .body_factory_info controls error response MIME type'
Test.ContinueOnFail = True


class BodyFactoryContentTypeTest:
    """
    Test that the Content-Type directive in .body_factory_info is used for
    body factory error responses instead of the hardcoded text/html default.

    Two scenarios:
    1. Default: no Content-Type directive -> text/html; charset=utf-8
    2. Custom: Content-Type: text/plain -> text/plain; charset=utf-8
    """

    def __init__(self):
        self._setupDefaultTS()
        self._setupCustomTS()

    def _setupDefaultTS(self):
        """ATS instance with default body factory (no Content-Type directive)."""
        self._ts_default = Test.MakeATSProcess("ts_default")
        self._ts_default.Disk.records_config.update(
            {
                'proxy.config.body_factory.enable_customizations': 1,
                'proxy.config.url_remap.remap_required': 1,
            })
        self._ts_default.Disk.remap_config.AddLine('map http://mapped.example.com http://127.0.0.1:65535')

        body_factory_dir = self._ts_default.Variables.BODY_FACTORY_TEMPLATE_DIR
        info_path = os.path.join(body_factory_dir, 'default', '.body_factory_info')
        self._ts_default.Disk.File(info_path).WriteOn("Content-Language: en\nContent-Charset: utf-8\n")

    def _setupCustomTS(self):
        """ATS instance with Content-Type: text/plain in .body_factory_info."""
        self._ts_custom = Test.MakeATSProcess("ts_custom")
        self._ts_custom.Disk.records_config.update(
            {
                'proxy.config.body_factory.enable_customizations': 1,
                'proxy.config.url_remap.remap_required': 1,
            })
        self._ts_custom.Disk.remap_config.AddLine('map http://mapped.example.com http://127.0.0.1:65535')

        body_factory_dir = self._ts_custom.Variables.BODY_FACTORY_TEMPLATE_DIR
        info_path = os.path.join(body_factory_dir, 'default', '.body_factory_info')
        self._ts_custom.Disk.File(info_path).WriteOn("Content-Type: text/plain\n")

    def run(self):
        self._testDefaultContentType()
        self._testCustomContentType()

    def _testDefaultContentType(self):
        """Without Content-Type directive, error responses should use text/html."""
        tr = Test.AddTestRun('Default body factory Content-Type is text/html')
        tr.Processes.Default.StartBefore(self._ts_default)
        tr.Processes.Default.Command = (
            f'curl -s -D- -o /dev/null'
            f' -H "Host: unmapped.example.com"'
            f' http://127.0.0.1:{self._ts_default.Variables.port}/')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.TimeOut = 5
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'Content-Type: text/html', 'Default body factory should produce text/html')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('HTTP/1.1 404', 'Unmapped request should get 404')
        tr.StillRunningAfter = self._ts_default

    def _testCustomContentType(self):
        """With Content-Type: text/plain, error responses should use text/plain."""
        tr = Test.AddTestRun('Custom body factory Content-Type is text/plain')
        tr.Processes.Default.StartBefore(self._ts_custom)
        tr.Processes.Default.Command = (
            f'curl -s -D- -o /dev/null'
            f' -H "Host: unmapped.example.com"'
            f' http://127.0.0.1:{self._ts_custom.Variables.port}/')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.TimeOut = 5
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'Content-Type: text/plain; charset=utf-8', 'Custom body factory should produce text/plain with charset')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('HTTP/1.1 404', 'Unmapped request should get 404')
        tr.StillRunningAfter = self._ts_custom


BodyFactoryContentTypeTest().run()
