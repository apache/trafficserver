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

Test.Sumary = '''
Verify Empty DATA Frame Handling
'''


class Http2EmptyDataFrameTest:

    def __init__(self):
        self.__setupOriginServer()
        self.__setupTS()
        self.__setupClient()

    def __setupOriginServer(self):
        self._server = Test.MakeHttpBinServer("httpbin")

    def __setupTS(self):
        self._ts = Test.MakeATSProcess(f"ts", enable_tls=True, enable_cache=True)
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http2',
                'proxy.config.ssl.server.cert.path': f"{self._ts.Variables.SSLDir}",
                'proxy.config.ssl.server.private_key.path': f"{self._ts.Variables.SSLDir}",
                'proxy.config.http.insert_response_via_str': 2,
                'proxy.config.http2.active_timeout_in': 3,
                'proxy.config.http2.stream_error_rate_threshold': 0.1  # default
            })
        self._ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self._server.Variables.Port}")
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

    def __setupClient(self):
        self._ts.Setup.CopyAs("clients/h2empty_data_frame.py", Test.RunDirectory)

    def run(self):
        tr = Test.AddTestRun("warm-up cache")

        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.StartBefore(self._server)

        # warm up the cache
        tr.Processes.Default.Command = f"{sys.executable} h2empty_data_frame.py {self._ts.Variables.ssl_port} /cache/10 -n 1"
        tr.Processes.Default.ReturnCode = 0

        # verify 20 streams doesn't hit `proxy.config.http2.stream_error_rate_threshold`
        tr = Test.AddTestRun("open 20 streams")
        tr.Processes.Default.Command = f"{sys.executable} h2empty_data_frame.py {self._ts.Variables.ssl_port} /cache/10 -n 20"
        tr.Processes.Default.ReturnCode = 0

        tr.StillRunningAfter = self._ts


Http2EmptyDataFrameTest().run()
