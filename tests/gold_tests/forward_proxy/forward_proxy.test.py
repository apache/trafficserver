"""
"""
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

from typing import Union

Test.Summary = 'Verify ATS can function as a forward proxy'
Test.ContinueOnFail = True


class ForwardProxyTest:
    _scheme_proto_mismatch_policy: Union[int, None]
    _ts_counter: int = 0
    _server_counter: int = 0

    def __init__(self, verify_scheme_matches_protocol: Union[int, None]):
        """Construct a ForwardProxyTest object.

        :param verify_scheme_matches_protocol: The value with which to
        configure Traffic Server's
        proxy.config.ssl.client.scheme_proto_mismatch_policy. A value of None
        means that no value will be explicitly set in the records.config.
        :type verify_scheme_matches_protocol: int or None
        """
        self._scheme_proto_mismatch_policy = verify_scheme_matches_protocol
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        """Configure the Proxy Verifier server."""
        proc_name = f"server{ForwardProxyTest._server_counter}"
        self.server = Test.MakeVerifierServerProcess(proc_name, "forward_proxy.replay.yaml")
        ForwardProxyTest._server_counter += 1
        if self._scheme_proto_mismatch_policy in (2, None):
            self.server.Streams.All = Testers.ExcludesExpression(
                'Received an HTTP/1 request with key 1',
                'Verify that the server did not receive the request.')
        else:
            self.server.Streams.All = Testers.ContainsExpression(
                'Received an HTTP/1 request with key 1',
                'Verify that the server received the request.')

    def setupTS(self):
        """Configure the Traffic Server process."""
        proc_name = f"ts{ForwardProxyTest._ts_counter}"
        self.ts = Test.MakeATSProcess(proc_name, enable_tls=True, enable_cache=False)
        ForwardProxyTest._ts_counter += 1
        self.ts.addDefaultSSLFiles()
        self.ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")
        self.ts.Disk.remap_config.AddLine(
            f"map / http://127.0.0.1:{self.server.Variables.http_port}/")

        self.ts.Disk.records_config.update({
            'proxy.config.ssl.server.cert.path': self.ts.Variables.SSLDir,
            'proxy.config.ssl.server.private_key.path': self.ts.Variables.SSLDir,
            'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
            'proxy.config.ssl.keylog_file': '/tmp/keylog.txt',

            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': "http",
        })

        if self._scheme_proto_mismatch_policy is not None:
            self.ts.Disk.records_config.update({
                'proxy.config.ssl.client.scheme_proto_mismatch_policy': self._scheme_proto_mismatch_policy,
            })

    def addProxyHttpsToHttpCase(self):
        """Test ATS as an HTTPS forward proxy behind an HTTP server."""
        tr = Test.AddTestRun()
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.Command = (
            f'curl --proxy-insecure -v -H "uuid: 1" '
            f'--proxy "https://127.0.0.1:{self.ts.Variables.ssl_port}/" '
            f'http://example.com/')
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

        if self._scheme_proto_mismatch_policy in (2, None):
            tr.Processes.Default.Streams.All = Testers.ContainsExpression(
                '< HTTP/1.1 400 Invalid HTTP Request',
                'Verify that the request was rejected.')
        else:
            tr.Processes.Default.Streams.All = Testers.ContainsExpression(
                '< HTTP/1.1 200 OK',
                'Verify that curl received a 200 OK response.')

    def run(self):
        """Configure the TestRun instances for this set of tests."""
        self.addProxyHttpsToHttpCase()


ForwardProxyTest(verify_scheme_matches_protocol=None).run()
ForwardProxyTest(verify_scheme_matches_protocol=0).run()
ForwardProxyTest(verify_scheme_matches_protocol=1).run()
ForwardProxyTest(verify_scheme_matches_protocol=2).run()
