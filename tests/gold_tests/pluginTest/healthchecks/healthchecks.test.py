"""
Verify healthchecks plugin behavior.
"""
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  for additional information
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

Test.Summary = '''Verify healthchecks plugin behavior.'''

Test.SkipUnless(Condition.PluginExists('healthchecks.so'))

CONFIG = f'''
/acme {Test.RunDirectory}/acme text/plain 200 404
/acme-ssl {Test.RunDirectory}/acme-ssl text/plain 200 404
'''

CONTENT = 'Some generic content.'


class TestFileChangeBehavior:
    '''Verify healthchecks plugin file change detection behavior.'''

    def __init__(self) -> None:
        '''Initialize the TestRun.'''
        self._ts_started = False
        self._positive_hc_counter = 0
        self._configure_global_ts()
        self._expect_positive_healthchecks()
        self._remove_acme_ssl()
        self._expect_acme_ssl_404()
        self._re_add_acme_ssl()
        self._expect_positive_healthchecks()
        self._expect_full_buffer_acme_body()
        self._rewrite_acme_while_serving()
        self._expect_rewritten_acme_body()

    def _configure_global_ts(self) -> None:
        '''Configure a global Traffic Server instance for the test runs.
        :param tr: The TestRun to associate the ATS instance with.
        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess('ts', enable_tls=True)
        self._ts = ts

        # healthchecks plugin configuration.
        config_name = "healthchecks.config"
        config_path = os.path.join(ts.Variables.CONFIGDIR, config_name)
        ts.Disk.File(config_path, id='healthchecks_config', typename="ats:config")
        config = ts.Disk.healthchecks_config
        config.AddLine(f'/acme {Test.RunDirectory}/acme text/plain 200 404')
        config.AddLine(f'/acme-ssl {Test.RunDirectory}/acme-ssl text/plain 200 404')
        ts.Setup.Copy('acme')
        ts.Setup.Copy('acme-ssl')
        ts.Disk.plugin_config.AddLine(f'healthchecks.so {ts.Variables.CONFIGDIR}/healthchecks.config')

        # TLS configuration.
        ts.addDefaultSSLFiles()
        ts.Disk.records_config.update(
            {
                "proxy.config.ssl.server.cert.path": f'{ts.Variables.SSLDir}',
                "proxy.config.ssl.server.private_key.path": f'{ts.Variables.SSLDir}',
                "proxy.config.ssl.client.verify.server.policy": 'PERMISSIVE',
            })
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

        # Other configuration.
        ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'healthchecks',
        })
        return ts

    def _expect_positive_healthchecks(self) -> None:
        '''Configure a positive healthcheck for the test runs.
        :return: None
        '''
        self._positive_hc_counter += 1
        counter = self._positive_hc_counter
        tr = Test.AddTestRun(f'Positive acme healthchecks: {counter}')
        tr.MakeCurlCommand(f'-v http://127.0.0.1:{self._ts.Variables.port}/acme', ts=self._ts)
        curl_acme = tr.Processes.Default
        if not self._ts_started:
            curl_acme.StartBefore(self._ts)
            self._ts_started = True
        curl_acme.Streams.All += Testers.ContainsExpression('HTTP/1.1 200', 'Verify 200 response for /acme')

        if not Condition.CurlUsingUnixDomainSocket():
            # Repeat for acme-ssl
            tr2 = Test.AddTestRun(f'Positive acme-ssl healthchecks: {counter}')
            tr2.MakeCurlCommand(f'-kv https://127.0.0.1:{self._ts.Variables.ssl_port}/acme-ssl', ts=self._ts)
            curl_acme_ssl = tr2.Processes.Default
            if not self._ts_started:
                curl_acme_ssl.StartBefore(self._ts)
                self._ts_started = True
            curl_acme_ssl.Streams.All += Testers.ContainsExpression('HTTP/2 200', 'Verify 200 response for /acme-ssl')

    def _remove_acme_ssl(self) -> None:
        '''Remove the acme-ssl file to trigger a file change detection.
        :return: None
        '''
        if not Condition.CurlUsingUnixDomainSocket():
            tr = Test.AddTestRun('Remove acme-ssl file')
            p = tr.Processes.Default
            p.Command = f'rm {Test.RunDirectory}/acme-ssl && sleep 1'
            tr.Processes.Default.ReturnCode = 0

    def _expect_acme_ssl_404(self) -> None:
        '''Expect a 404 response for the removed acme-ssl file.
        :return: None
        '''
        tr = Test.AddTestRun('Expect 200 for acme after acme-ssl removal')
        tr.MakeCurlCommand(f'-v http://127.0.0.1:{self._ts.Variables.port}/acme', ts=self._ts)
        curl_acme = tr.Processes.Default
        if not self._ts_started:
            curl_acme.StartBefore(self._ts)
            self._ts_started = True
        curl_acme.Streams.All += Testers.ContainsExpression('HTTP/1.1 200', 'Verify 200 response for /acme after acme-ssl removal')

        if not Condition.CurlUsingUnixDomainSocket():
            tr2 = Test.AddTestRun('Expect 404 for acme-ssl after removal')
            tr2.MakeCurlCommand(f'-kv https://127.0.0.1:{self._ts.Variables.ssl_port}/acme-ssl', ts=self._ts)
            curl_acme_ssl = tr2.Processes.Default
            if not self._ts_started:
                curl_acme_ssl.StartBefore(self._ts)
                self._ts_started = True
            curl_acme_ssl.Streams.All += Testers.ContainsExpression('HTTP/2 404', 'Verify 404 response for /acme-ssl after removal')

    def _re_add_acme_ssl(self) -> None:
        '''Re-add the acme-ssl file to restore the healthcheck.
        :return: None
        '''
        if not Condition.CurlUsingUnixDomainSocket():
            tr = Test.AddTestRun('Re-add acme-ssl file')
            tr.Setup.Copy('acme-ssl', Test.RunDirectory)
            p = tr.Processes.Default
            p.Command = 'sleep 1'
            p.ReturnCode = 0

    def _rewrite_acme_while_serving(self) -> None:
        '''Rewrite the acme file repeatedly while healthcheck requests are in flight.

        The plugin replaces the health check file data underneath transactions which may still be
        reading the previous data. This drives that replacement so that an ASan enabled build
        catches the old data being released while it is still referenced.
        :return: None
        '''
        tr = Test.AddTestRun('Rewrite acme while healthchecks are being served')
        acme_file = os.path.join(Test.RunDirectory, 'acme')
        url = f'http://127.0.0.1:{self._ts.Variables.port}/acme'

        # Note that autest runs the command through string.Template, so shell variables cannot be
        # used here. The loop is therefore unrolled.
        commands = []
        for iteration in range(10):
            commands.append(f'echo "{CONTENT} {iteration}" > {acme_file};')
            commands.append('{curl} -s -o /dev/null ' + url + ' &')
            commands.append('{curl} -s -o /dev/null ' + url + ' &')
        commands.append('wait')

        tr.MakeCurlCommandMulti(' '.join(commands), ts=self._ts)
        tr.Processes.Default.ReturnCode = 0

    def _expect_full_buffer_acme_body(self) -> None:
        '''Verify that a MAX_BODY_LEN-sized file is not reported as empty.
        :return: None
        '''
        tr = Test.AddTestRun('Expect a full-sized healthcheck response body')
        acme_file = os.path.join(Test.RunDirectory, 'acme')
        url = f'http://127.0.0.1:{self._ts.Variables.port}/acme'
        command = (f'dd if=/dev/zero of={acme_file} bs=16384 count=1 2>/dev/null && sleep 1 && ' + '{curl} -s ' + url + ' | wc -c')
        tr.MakeCurlCommandMulti(command, ts=self._ts)
        p = tr.Processes.Default
        p.ReturnCode = 0
        p.Streams.All += Testers.ContainsExpression('16384', 'Verify the response contains 16 KiB')

    def _expect_rewritten_acme_body(self) -> None:
        '''Verify that the most recently written acme content is what gets served.
        :return: None
        '''
        tr = Test.AddTestRun('Expect the last written acme content in the response body')
        acme_file = os.path.join(Test.RunDirectory, 'acme')
        url = f'http://127.0.0.1:{self._ts.Variables.port}/acme'
        command = f'echo "{CONTENT} final" > {acme_file} && sleep 1 && ' + '{curl} -v ' + url
        tr.MakeCurlCommandMulti(command, ts=self._ts)
        p = tr.Processes.Default
        p.ReturnCode = 0
        p.Streams.All += Testers.ContainsExpression('HTTP/1.1 200', 'Verify 200 response for /acme')
        p.Streams.All += Testers.ContainsExpression(f'{CONTENT} final', 'Verify the reloaded acme content is served')


# Instantiate the test
TestFileChangeBehavior()
