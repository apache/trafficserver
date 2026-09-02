'''
Test default server certificate updates through TSSslSecretSet/TSSslSecretUpdate.
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
import re

Test.Summary = '''
Verify that secret updates refresh the default server SSL_CTX used without SNI.
'''


class TestDefaultSecretUpdate:
    '''Verify default server certificate updates through the secret API.'''

    initial_cert: str = 'signed-bar.pem'
    updated_cert: str = 'signed2-bar.pem'
    key_file: str = 'signed-bar.key'
    shadowed_cert: str = 'signed-foo.pem'
    updated_shadowed_cert: str = 'signed2-foo.pem'
    shadowed_key_file: str = 'signed-foo.key'
    plugin: str = 'ssl_secret_load_test.so'

    def __init__(self) -> None:
        '''Configure the ATS process, origin server, and test runs.'''
        self._configure_server()
        self._configure_traffic_server()
        self._add_start_run()
        self._add_config_touch_run()
        self._add_config_reload_run()
        self._add_config_reload_wait_run()
        self._add_initial_certificate_run()
        self._add_mtime_delay_run()
        self._add_shadowed_certificate_update_run()
        self._add_secret_update_wait_run(self.shadowed_cert)
        self._add_unchanged_default_certificate_run()
        self._add_default_certificate_update_run()
        self._add_secret_update_wait_run(self.initial_cert)
        self._add_updated_certificate_run()

    def _configure_server(self) -> 'Process':
        '''Configure the origin server.'''
        server = Test.MakeOriginServer('server')
        self._server = server

        request_header = {'headers': 'GET / HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n', 'timestamp': '1469733493.993', 'body': ''}
        response_header = {'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n', 'timestamp': '1469733493.993', 'body': ''}
        server.addResponse('sessionlog.json', request_header, response_header)
        return server

    def _configure_traffic_server(self) -> 'Process':
        '''Configure the Traffic Server process.'''
        ts = Test.MakeATSProcess('ts', enable_tls=True)
        self._ts = ts

        ts.addSSLfile(f'ssl/{self.initial_cert}')
        ts.addSSLfile(f'ssl/{self.updated_cert}')
        ts.addSSLfile(f'ssl/{self.key_file}')
        ts.addSSLfile(f'ssl/{self.shadowed_cert}')
        ts.addSSLfile(f'ssl/{self.updated_shadowed_cert}')
        ts.addSSLfile(f'ssl/{self.shadowed_key_file}')
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, self.plugin), ts)

        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'ssl_secret_load_test',
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}/../',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}/../',
                'proxy.config.ssl.server.multicert.concurrency': 1,
                'proxy.config.exec_thread.autoconfig.scale': 1.0,
                'proxy.config.url_remap.pristine_host_hdr': 1,
            })

        ts.Disk.ssl_multicert_yaml.AddLines(
            [
                'ssl_multicert:',
                '  - dest_ip: "*"',
                f'    ssl_cert_name: {self.initial_cert}',
                f'    ssl_key_name: {self.key_file}',
                '  - dest_ip: "*"',
                f'    ssl_cert_name: {self.shadowed_cert}',
                f'    ssl_key_name: {self.shadowed_key_file}',
            ])
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}')
        return ts

    def _add_start_run(self) -> 'TestRun':
        '''Start ATS before reloading the multicert configuration serially.'''
        tr = Test.AddTestRun('Start ATS')
        tr.Processes.Default.Command = 'echo starting ATS'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        self._keep_processes_running(tr)
        return tr

    def _add_config_reload_run(self) -> 'TestRun':
        '''Reload the multicert configuration with deterministic ordering.'''
        tr = Test.AddTestRun('Reload the multicert configuration serially')
        tr.Processes.Default.Command = 'traffic_ctl config reload'
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.ReturnCode = 0
        self._keep_processes_running(tr)
        return tr

    def _add_config_touch_run(self) -> 'TestRun':
        '''Mark the multicert configuration for reload.'''
        tr = Test.AddTestRun('Mark the multicert configuration for reload')
        tr.Processes.Default.Command = f'touch {self._ts.Disk.ssl_multicert_yaml.AbsPath}'
        tr.Processes.Default.ReturnCode = 0
        self._keep_processes_running(tr)
        return tr

    def _add_config_reload_wait_run(self) -> 'TestRun':
        '''Wait for the serial multicert configuration reload to finish.'''
        tr = Test.AddTestRun('Wait for the multicert configuration reload')
        tr.Processes.Default.Command = 'echo awaiting multicert reload'
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.ReturnCode = 0
        await_reload = tr.Processes.Process('await_reload', 'sleep 30')
        await_reload.Ready = When.FileContains(self._ts.Disk.diags_log.Name, 'ssl_multicert.yaml finished loading', 2)
        tr.Processes.Default.StartBefore(await_reload)
        self._keep_processes_running(tr)
        return tr

    def _add_initial_certificate_run(self) -> 'TestRun':
        '''Verify the original default certificate before the secret update.'''
        tr = self._add_curl_run(
            'Initial default certificate',
            expected_cn='signer.yahoo.com',
            unexpected_cn='signer2.yahoo.com',
            expected_description='Initial cert uses signer.',
            unexpected_description='Initial cert is not updated yet.')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(r'subject:.*CN=bar\.com', 'Default cert is bar.com.')
        tr.Processes.Default.Streams.All += Testers.ExcludesExpression(r'subject:.*CN=foo\.com', 'Shadowed cert is not served.')
        return tr

    def _add_mtime_delay_run(self) -> 'TestRun':
        '''Make the replacement certificate mtime differ from the original.'''
        tr = Test.AddTestRun('Make the cert mtime differ')
        tr.Processes.Default.Command = 'sleep 2'
        tr.Processes.Default.ReturnCode = 0
        self._keep_processes_running(tr)
        return tr

    def _add_shadowed_certificate_update_run(self) -> 'TestRun':
        '''Replace the certificate for the shadowed wildcard policy.'''
        tr = Test.AddTestRun('Update the shadowed wildcard certificate')
        tr.Setup.CopyAs(f'ssl/{self.updated_shadowed_cert}', '.', f'{self._ts.Variables.SSLDir}/{self.shadowed_cert}')
        tr.Processes.Default.Command = f'touch {self._ts.Variables.SSLDir}/{self.shadowed_cert}'
        tr.Processes.Default.ReturnCode = 0
        self._keep_processes_running(tr)
        return tr

    def _add_unchanged_default_certificate_run(self) -> 'TestRun':
        '''Verify that refreshing a shadowed wildcard does not replace the default.'''
        tr = self._add_curl_run(
            'Default certificate unchanged after shadowed wildcard update',
            expected_cn='signer.yahoo.com',
            unexpected_cn='signer2.yahoo.com',
            expected_description='Default cert still uses signer.',
            unexpected_description='Shadowed wildcard cert did not become the default.')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(r'subject:.*CN=bar\.com', 'Default cert is still bar.com.')
        tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
            r'subject:.*CN=foo\.com', 'Shadowed foo.com cert is not served.')
        return tr

    def _add_default_certificate_update_run(self) -> 'TestRun':
        '''Replace the plugin backing default certificate file.'''
        tr = Test.AddTestRun('Update the default certificate')
        tr.Setup.CopyAs(f'ssl/{self.updated_cert}', '.', f'{self._ts.Variables.SSLDir}/{self.initial_cert}')
        tr.Processes.Default.Command = f'touch {self._ts.Variables.SSLDir}/{self.initial_cert}'
        tr.Processes.Default.ReturnCode = 0
        self._keep_processes_running(tr)
        return tr

    def _add_secret_update_wait_run(self, secret_name: str) -> 'TestRun':
        '''Wait for the test plugin to update the certificate secret.'''
        tr = Test.AddTestRun(f'Wait for the {secret_name} secret update')
        tr.Processes.Default.Command = 'echo awaiting secret update'
        tr.Processes.Default.ReturnCode = 0
        await_update = tr.Processes.Process('await_update', 'sleep 30')
        await_update.Ready = When.FileContains(
            self._ts.Disk.traffic_out.Name, f'updated cert for secret .*{re.escape(secret_name)}')
        tr.Processes.Default.StartBefore(await_update)
        self._keep_processes_running(tr)
        return tr

    def _add_updated_certificate_run(self) -> 'TestRun':
        '''Verify the updated default certificate after the secret update.'''
        return self._add_curl_run(
            'Updated default certificate',
            expected_cn='signer2.yahoo.com',
            unexpected_cn='signer.yahoo.com',
            expected_description='Updated cert uses signer2.',
            unexpected_description='Updated cert no longer uses signer.')

    def _add_curl_run(
            self, name: str, expected_cn: str, unexpected_cn: str, expected_description: str,
            unexpected_description: str) -> 'TestRun':
        '''Add a curl run against ATS without SNI.'''
        tr = Test.AddTestRun(name)
        self._keep_processes_running(tr)
        tr.MakeCurlCommand(
            f"-k -v --http1.1 -H 'host: doesnotmatter' https://127.0.0.1:{self._ts.Variables.ssl_port}/", ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(f'issuer:.*CN={re.escape(expected_cn)}', expected_description)
        tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
            f'issuer:.*CN={re.escape(unexpected_cn)}', unexpected_description)
        return tr

    def _keep_processes_running(self, tr: 'TestRun') -> None:
        '''Keep ATS and the origin server running after a TestRun.'''
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server


TestDefaultSecretUpdate()
