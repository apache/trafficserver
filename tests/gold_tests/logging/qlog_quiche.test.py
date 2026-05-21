'''
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

Test.Summary = '''
Basic test for qlog using quiche library.
'''

Test.SkipUnless(Condition.HasATSFeature('TS_HAS_QUICHE'))

Generate_Qlog = True
No_Qlog = False


class quiche_qlog_Test:
    replay_file = "replay/basic1.replay.yaml"
    client_counter: int = 0
    ts_counter: int = 0
    server_counter: int = 0

    def __init__(self, name: str):
        """Initialize the test.
        :param name: The name of the test.
        """
        self._name = name
        self._generate_qlog = False

    def with_qlogs(self):
        self._generate_qlog = True
        return self

    def without_qlogs(self):
        self._generate_qlog = False
        return self

    def _configure_server(self, tr: 'TestRun'):
        """Configure the server.

        :param tr: The TestRun object to associate the server process with.
        """
        server = tr.AddVerifierServerProcess(f"server_{quiche_qlog_Test.server_counter}", self.replay_file)
        quiche_qlog_Test.server_counter += 1
        self._server = server

    def _configure_traffic_server(self, tr: 'TestRun'):
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        ts = Test.MakeATSProcess(f"ts-{quiche_qlog_Test.ts_counter}", enable_quic=True, enable_tls=True)
        self._ts = ts
        quiche_qlog_Test.ts_counter += 1
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.records_config.update(
            '''
        diags:
          debug:
            enabled: 1
            tags: vv_quic|v_quic
        quic:
          no_activity_timeout_in: 3000
        ''')
        if self._generate_qlog:
            self._ts.Disk.records_config.update(
                '''
                  quic:
                    qlog:
                      file_base: log/test_qlog # we expect to have log/test_qlog-<TRACE ID>.sqlog
                  ''')
        self._ts.Disk.ssl_multicert_config.AddLine(
            f'dest_ip=* ssl_cert_name={ts.Variables.SSLDir}/server.pem ssl_key_name={ts.Variables.SSLDir}/server.key')

        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')

    def test(self):
        """Run the test."""
        tr = Test.AddTestRun(self._name)
        self._configure_server(tr)
        self._configure_traffic_server(tr)

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.AddVerifierClientProcess(
            f"client-{quiche_qlog_Test.client_counter}", self.replay_file, http3_ports=[self._ts.Variables.ssl_port])
        quiche_qlog_Test.client_counter += 1
        tr.Processes.Default.ReturnCode = 0

        # If requested we will copy the file to a known name as the runtime name is not know
        # by the test. So we will rename them all and grab the first one for validation.
        test_run = Test.AddTestRun(f"{self._name} : check qlog files")
        qlog_base_name = "test_qlog"
        rename_script = os.path.join(Test.TestDirectory, 'rename_qlog.sh')
        test_run.Processes.Default.Command = f'sleep 5; bash {rename_script} {self._ts.Variables.LOGDIR} {qlog_base_name}'
        test_run.Processes.Default.ReturnCode = 0 if self._generate_qlog else 2  # exit 2 is what we want if no qlog was generated.

        # Basic valdation
        if self._generate_qlog:
            tr = Test.AddTestRun("Check qlog content")
            file = os.path.join(self._ts.Variables.LOGDIR, "1.sqlog")
            f = tr.Disk.File(file)
            tr.Processes.Default.Command = "echo 0"
            tr.Processes.Default.ReturnCode = 0
            f.Content = Testers.IncludesExpression('"title":"Apache Traffic Server"', 'Should include this basic text')


quiche_qlog_Test("Generate qlog test").with_qlogs().test()
quiche_qlog_Test("Do not generate qlog").without_qlogs().test()
