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

from tools.uranium.scenario import All, Any, Condition, Testers, UraniumTest, When


def test_sslheaders(urtest: UraniumTest) -> None:
    '''
    Test the sslheaders plugin.
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

    urtest.Summary = '''
    Test sslheaders plugin.
    '''

    urtest.SkipIf(Condition.CurlUsingUnixDomainSocket())
    urtest.SkipUnless(
        Condition.PluginExists('sslheaders.so'),
        Condition.HasCurlFeature('http2'),
    )

    urtest.Disk.File('sslheaders.log').Content = 'sslheaders.gold'

    server = urtest.MakeOriginServer("server", options={'--load': urtest.TestDirectory + '/observer.py'})

    request_header = {"headers": "GET / HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
    response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
    server.addResponse("sessionlog.json", request_header, response_header)

    # Disable the cache to make sure each request is forwarded to the origin
    # server.
    ts = urtest.MakeATSProcess("ts", enable_tls=True, enable_cache=False)

    ts.addDefaultSSLFiles()
    # ts.addSSLfile("ssl/signer.pem")

    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 0,
            'proxy.config.diags.debug.tags': 'http',
            'proxy.config.proxy_name': 'Poxy_Proxy',  # This will be the server name.
            'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.http.server_ports':
                (
                    'ipv4:{0} ipv4:{1}:proto=http2;http:ssl ipv6:{0} ipv6:{1}:proto=http2;http:ssl'.format(
                        ts.Variables.port, ts.Variables.ssl_port)),
        })

    ts.Disk.ssl_multicert_yaml.AddLines(
        """
    ssl_multicert:
      - dest_ip: "*"
        ssl_cert_name: server.pem
        ssl_key_name: server.key
    """.split("\n"))

    ts.Disk.remap_config.AddLine('map http://bar.com http://127.0.0.1:{0}'.format(server.Variables.Port))
    ts.Disk.remap_config.AddLine('map https://bar.com http://127.0.0.1:{0}'.format(server.Variables.Port))

    ts.Disk.plugin_config.AddLine('sslheaders.so SSL-Client-ID=client.subject')

    tr = urtest.AddTestRun()
    tr.Processes.Default.StartBefore(server)
    tr.Processes.Default.StartBefore(urtest.Processes.ts)
    tr.MakeCurlCommand(
        '-H "SSL-Client-ID: My Fake Client ID" --verbose --ipv4 --insecure --header "Host: bar.com"' +
        ' https://localhost:{}'.format(ts.Variables.ssl_port),
        ts=ts)
    tr.Processes.Default.ReturnCode = 0
    urtest.execute()
