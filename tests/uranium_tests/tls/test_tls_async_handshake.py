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

from uranium_testkit.scenario import All, Any, Condition, Testers, UraniumTest, When


def test_tls_async_handshake(urtest: UraniumTest) -> None:
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

    urtest.Summary = '''
    Test TLS handshakes through OpenSSL's async job interface.
    '''

    async_handshake = os.path.join(urtest.Variables.AtsTestPluginsDir, 'async_handshake.so')

    urtest.SkipUnless(
        Condition.HasOpenSSLVersion('1.1.1'),
        Condition.IsOpenSSL(),
        Condition(lambda: os.path.isfile(async_handshake), async_handshake + " not found."),
    )

    ts = urtest.MakeATSProcess("ts", enable_tls=True)
    server = urtest.MakeOriginServer("server")

    if os.path.isfile(async_handshake):
        urtest.PrepareTestPlugin(async_handshake, ts)

    server.addResponse(
        "sessionlog.json", {
            "headers": "GET / HTTP/1.1\r\nuuid: basic\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }, {
            "headers":
                "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n"
                "Cache-Control: max-age=3600\r\nContent-Length: 2\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": "ok"
        })

    ts.addSSLfile("ssl/server.pem")
    ts.addSSLfile("ssl/server.key")

    ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

    ts.Disk.ssl_multicert_yaml.AddLines(
        """
    ssl_multicert:
      - dest_ip: "*"
        ssl_cert_name: server.pem
        ssl_key_name: server.key
    """.split("\n"))
    ts.Disk.records_config.update(
        {
            'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.exec_thread.autoconfig.scale': 1.0,
            'proxy.config.ssl.async.handshake.enabled': 1,
            'proxy.config.diags.debug.enabled': 0,
            'proxy.config.diags.debug.tags': 'ssl|http'
        })

    tr = urtest.AddTestRun("Run-Test")
    tr.MakeCurlCommand("-k -v -H uuid:basic -H host:example.com  https://127.0.0.1:{0}/".format(ts.Variables.ssl_port), ts=ts)
    tr.ReturnCode = 0
    tr.Processes.Default.StartBefore(server)
    tr.Processes.Default.StartBefore(urtest.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
    tr.Processes.Default.Streams.All = Testers.ContainsExpression(r"HTTP/(2|1\.1) 200", "Request succeeds")
    tr.StillRunningAfter = server

    ts.Disk.traffic_out.Content += Testers.ContainsExpression("resumed OpenSSL async job", "The OpenSSL async job resumes")
    urtest.execute()
