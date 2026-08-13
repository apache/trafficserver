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


def test_tls_hooks4(urtest: UraniumTest) -> None:
    '''
    Test 1 preaccept, 1 sni, and 1 cert callback (with delay)
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

    urtest.Summary = '''
    Test different combinations of TLS handshake hooks to ensure they are applied consistently.
    '''

    ts = urtest.MakeATSProcess("ts", enable_tls=True)
    server = urtest.MakeOriginServer("server")
    request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
    # desired response form the origin server
    response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
    server.addResponse("sessionlog.json", request_header, response_header)

    ts.addDefaultSSLFiles()

    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.show_location': 0,
            'proxy.config.diags.debug.tags': 'ssl_hook_test',
            'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        })

    ts.Disk.ssl_multicert_yaml.AddLines(
        """
    ssl_multicert:
      - dest_ip: "*"
        ssl_cert_name: server.pem
        ssl_key_name: server.key
    """.split("\n"))

    ts.Disk.remap_config.AddLine(
        'map https://example.com:{0} http://127.0.0.1:{1}'.format(ts.Variables.ssl_port, server.Variables.Port))

    urtest.PrepareTestPlugin(
        os.path.join(urtest.Variables.AtsTestPluginsDir, 'ssl_hook_test.so'), ts, '-cert=1 -sni=1 -preaccept=1')

    tr = urtest.AddTestRun("Test one sni, one preaccept, and one cert hook")
    tr.Processes.Default.StartBefore(server)
    tr.Processes.Default.StartBefore(urtest.Processes.ts)
    tr.StillRunningAfter = ts
    tr.StillRunningAfter = server
    tr.MakeCurlCommand('-k -H \'host:example.com:{0}\' https://127.0.0.1:{0}'.format(ts.Variables.ssl_port), ts=ts)
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = "gold/preaccept-1.gold"

    ts.Disk.traffic_out.Content = "gold/ts-preaccept1-sni1-cert1.gold"
    snistring = "SNI callback 0"
    preacceptstring = "Pre accept callback 0"
    certstring = "Cert callback 0"
    ts.Disk.traffic_out.Content = Testers.ContainsExpression(
        r"\A(?:(?!{0}).)*{0}(?!.*{0}).*\Z".format(snistring), "SNI message appears only once", reflags=re.S | re.M)
    # the preaccept may get triggered twice because the test framework creates a TCP connection before handing off to traffic_server
    ts.Disk.traffic_out.Content += Testers.ContainsExpression(
        r"\A(?:(?!{0}).)*{0}.*({0})?(?!.*{0}).*\Z".format(preacceptstring),
        "Pre accept message appears only once or twice",
        reflags=re.S | re.M)
    ts.Disk.traffic_out.Content += Testers.ContainsExpression(
        r"\A(?:(?!{0}).)*{0}(?!.*{0}).*\Z".format(certstring), "Cert message appears only once", reflags=re.S | re.M)

    tr.Processes.Default.TimeOut = 15
    tr.TimeOut = 15
    urtest.execute()
