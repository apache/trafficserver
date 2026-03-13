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

import sys

Test.Summary = '''
Verify that in-flight POST DATA frames are not dropped after ATS sends
GOAWAY due to a high stream error rate.

Regression test for: DATA frames for streams initiated before GOAWAY were
incorrectly ignored because the frame-reading loop exited on any non-zero
tx_error_code, including ENHANCE_YOUR_CALM. This caused in-flight POST
requests to time out with 408.
'''


class Http2PostAfterGoawayTest:
    _path = '/test'

    def __init__(self):
        self.__setupOriginServer()
        self.__setupTS()
        self.__setupClient()

    def __setupOriginServer(self):
        self._server = Test.MakeOriginServer("server")

        get_request = {
            "headers": f"GET {self._path} HTTP/1.1\r\nHost: localhost\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        get_response = {
            "headers": "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        # Register 4 GET responses (one per stream).
        for _ in range(4):
            self._server.addResponse("sessionlog.json", get_request, get_response)

        post_request = {
            "headers": f"POST {self._path} HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": "body"
        }
        post_response = {
            "headers": "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": "ok"
        }
        self._server.addResponse("sessionlog.json", post_request, post_response)

    def __setupTS(self):
        self._ts = Test.MakeATSProcess("ts", enable_tls=True, enable_cache=True)
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.http.cache.http': 0,
                'proxy.config.http.transaction_no_activity_timeout_in': 3,
                'proxy.config.ssl.server.cert.path': f"{self._ts.Variables.SSLDir}",
                'proxy.config.ssl.server.private_key.path': f"{self._ts.Variables.SSLDir}",
                # Lower thresholds so GOAWAY(ENHANCE_YOUR_CALM) fires after
                # a small number of stream errors:
                #   total >= 1/0.2 = 5 requests required
                #   GOAWAY when error_rate > min(1.0, 0.2*2) = 0.4
                #   3 errors / 5 total = 0.6 > 0.4 -> GOAWAY
                'proxy.config.http2.stream_error_rate_threshold': 0.2,
                'proxy.config.http2.stream_error_sampling_threshold': 1,
            })
        self._ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self._server.Variables.Port}")
        self._ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

    def __setupClient(self):
        self._ts.Setup.CopyAs("clients/h2_post_after_goaway.py", Test.RunDirectory)

    def run(self):
        tr = Test.AddTestRun()
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.Command = (f"{sys.executable} h2_post_after_goaway.py"
                                        f" {self._ts.Variables.ssl_port} {self._path}")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            "GOAWAY received with error_code=", "ATS must send GOAWAY before the POST completes")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            "SUCCESS", "POST request must complete with 200 OK after GOAWAY")
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server
        tr.TimeOut = 5


Http2PostAfterGoawayTest().run()
