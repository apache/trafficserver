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

Test.Summary = '''
Verify transaction data sink.
'''

Test.SkipUnless(Condition.PluginExists('txn_data_sink.so'),)


class TransactionDataSyncTest:

    replay_file = "transaction-with-body.replays.yaml"

    def __init__(self):
        self._setupOriginServer()
        self._setupNameserver()
        self._setupTS()

    def _setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("server", self.replay_file)

    def _setupNameserver(self):
        self.nameserver = Test.MakeDNServer("dns", default='127.0.0.1')

    def _setupTS(self):
        self.ts = Test.MakeATSProcess("ts", enable_cache=False, enable_tls=True)
        self.ts.Disk.records_config.update(
            {
                "proxy.config.ssl.server.cert.path": f'{self.ts.Variables.SSLDir}',
                "proxy.config.ssl.server.private_key.path": f'{self.ts.Variables.SSLDir}',
                "proxy.config.ssl.client.verify.server.policy": 'PERMISSIVE',
                "proxy.config.dns.nameservers": f"127.0.0.1:{self.nameserver.Variables.Port}",
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|txn_data_sink',
            })
        self.ts.addDefaultSSLFiles()
        self.ts.Disk.remap_config.AddLine(f'map / http://localhost:{self.server.Variables.http_port}/')
        self.ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        self.ts.Disk.plugin_config.AddLine('txn_data_sink.so')

        # All of the bodies that contained "not_dumped" were not configured to
        # be dumped. Therefore it is a bug if they show up in the logs.
        self.ts.Disk.traffic_out.Content += Testers.ExcludesExpression('body_not_dumped', "An unexpected body was dumped.")

        # Verify that each of the configured transaction bodies were dumped.
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            'http1.1_cl_response_body_dumped', "The expected HTTP/1.1 Content-Length response body was dumped.")
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            'http1.1_chunked_response_body_dumped', "The expected HTTP/1.1 chunked response body was dumped.")
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            'http1.1_cl_request_body_dumped', "The expected HTTP/1.1 Content-Length request body was dumped.")
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            'http1.1_chunked_request_body_dumped', "The expected HTTP/1.1 chunked request body was dumped.")
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            '"http2_response_body_dumped"', "The expected HTTP/2 response body was dumped.")
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            'http2_request_body_dumped', "The expected HTTP/2 request body was dumped.")

    def run(self):
        """Configure a TestRun for the test."""
        tr = Test.AddTestRun()
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.nameserver)
        tr.Processes.Default.StartBefore(self.ts)
        tr.AddVerifierClientProcess(
            "client",
            self.replay_file,
            http_ports=[self.ts.Variables.port],
            https_ports=[self.ts.Variables.ssl_port],
            other_args='--thread-limit 1')


TransactionDataSyncTest().run()
