'''
Verify support of external log rotation via SIGUSR2.
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
import shlex
import sys

TS_PID_SCRIPT = 'ts_process_handler.py'
ROTATE_DIAGS_SCRIPT = 'sigusr2_rotate_diags.sh'
ROTATE_CUSTOM_LOG_SCRIPT = 'sigusr2_rotate_custom_log.sh'


class Sigusr2Test:
    """
    Handle this test-specific Traffic Server configuration.
    """

    __ts_counter = 1
    __server_counter = 1

    @classmethod
    def _next_ts_name(cls):
        ts_name = f"sigusr2_ts{cls.__ts_counter}"
        cls.__ts_counter += 1
        return ts_name

    @classmethod
    def _next_server_name(cls):
        server_name = f"sigusr2_server{cls.__server_counter}"
        cls.__server_counter += 1
        return server_name

    @staticmethod
    def _make_script_command(script_name, *args):
        quoted_args = ' '.join(shlex.quote(str(arg)) for arg in args)
        return f"bash ./{script_name} {quoted_args}"

    def _configure_traffic_server(self, tr):
        ts_name = self._next_ts_name()
        ts = tr.MakeATSProcess(ts_name)
        ts.Disk.records_config.update(
            {
                'proxy.config.http.wait_for_cache': 1,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'log',
                'proxy.config.log.periodic_tasks_interval': 1,

                # All log rotation should be handled externally.
                'proxy.config.log.rolling_enabled': 0,
                'proxy.config.log.auto_delete_rolled_files': 0,
            })
        return ts, ts_name

    def _configure_server(self):
        server = Test.MakeOriginServer(self._next_server_name())

        for path in ['/first', '/second', '/third']:
            request_header = {
                'headers': f'GET {path} HTTP/1.1\r\nHost: does.not.matter\r\n\r\n',
                'timestamp': '1469733493.993',
                'body': ''
            }
            response_header = {
                'headers': 'HTTP/1.1 200 OK\r\n'
                           'Connection: close\r\n'
                           'Cache-control: max-age=85000\r\n\r\n',
                'timestamp': '1469733493.993',
                'body': 'xxx'
            }
            server.addResponse('sessionlog.json', request_header, response_header)

        return server

    def add_system_log_test(self):
        tr = Test.AddTestRun('Verify system logs can be rotated')
        ts, ts_name = self._configure_traffic_server(tr)

        rotated_diags_log = f'{ts.Disk.diags_log.AbsPath}_old'
        ts.Disk.File(rotated_diags_log, id='diags_log_old')

        tr.Processes.Default.Command = self._make_script_command(
            ROTATE_DIAGS_SCRIPT, sys.executable, f'./{TS_PID_SCRIPT}', ts_name, ts.Disk.diags_log.AbsPath, rotated_diags_log)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(ts)

        ts.Disk.diags_log.Content += Testers.ExcludesExpression(
            'traffic server running', 'The new diags.log should not reference the running traffic server')
        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            'Reseated diags.log', 'The new diags.log should indicate the newly opened diags.log')
        ts.Disk.diags_log_old.Content += Testers.ContainsExpression(
            'traffic server running', 'The rotated diags.log should keep the original startup message')

    def add_configured_log_test(self):
        tr = Test.AddTestRun('Verify yaml.log logs can be rotated')
        ts, ts_name = self._configure_traffic_server(tr)
        server = self._configure_server()

        ts.Disk.remap_config.AddLine(f'map http://127.0.0.1:{ts.Variables.port} http://127.0.0.1:{server.Variables.Port}')
        ts.Disk.logging_yaml.AddLine(
            '''
            logging:
              formats:
                - name: has_path
                  format: "%<pqu>: %<sssc>"
              logs:
                - filename: test_rotation
                  format: has_path
            ''')

        configured_log = os.path.join(ts.Variables.LOGDIR, 'test_rotation.log')
        ts.Disk.File(configured_log, id='configured_log')

        rotated_configured_log = f'{configured_log}_old'
        ts.Disk.File(rotated_configured_log, id='configured_log_old')

        ts.StartBefore(server)
        tr.Processes.Default.Command = self._make_script_command(
            ROTATE_CUSTOM_LOG_SCRIPT, sys.executable, f'./{TS_PID_SCRIPT}', ts_name, ts.Variables.port, configured_log,
            rotated_configured_log)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(ts)

        ts.Disk.configured_log.Content += Testers.ExcludesExpression(
            '/first', 'The new test_rotation.log should not have the first GET retrieval in it.')
        ts.Disk.configured_log.Content += Testers.ExcludesExpression(
            '/second', 'The new test_rotation.log should not have the second GET retrieval in it.')
        ts.Disk.configured_log.Content += Testers.ContainsExpression(
            '/third', 'The new test_rotation.log should have the third GET retrieval in it.')

        ts.Disk.configured_log_old.Content += Testers.ContainsExpression(
            '/first', 'test_rotation.log_old should have the first GET retrieval in it.')
        ts.Disk.configured_log_old.Content += Testers.ContainsExpression(
            '/second', 'test_rotation.log_old should have the second GET retrieval in it.')
        ts.Disk.configured_log_old.Content += Testers.ExcludesExpression(
            '/third', 'test_rotation.log_old should not have the third GET retrieval in it.')


Test.Summary = '''
Verify support of external log rotation via SIGUSR2.
'''

Test.Setup.Copy(TS_PID_SCRIPT)
Test.Setup.Copy(ROTATE_DIAGS_SCRIPT)
Test.Setup.Copy(ROTATE_CUSTOM_LOG_SCRIPT)

sigusr2_test = Sigusr2Test()
sigusr2_test.add_system_log_test()
sigusr2_test.add_configured_log_test()
