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
Test chunked encoding processing
'''

Test.SkipUnless(Condition.HasCurlFeature('http2'))
Test.ContinueOnFail = True

Test.GetTcpPort("upstream_port")

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")
server2 = Test.MakeOriginServer("server2", ssl=True)
server3 = Test.MakeOriginServer("server3")

testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nServer: uServer\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

request_header2 = {
    "headers":
        "POST / HTTP/1.1\r\nHost: www.anotherexample.com\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 11\r\n\r\n",
    "timestamp": "1415926535.898",
    "body": "knock knock"
}
response_header2 = {
    "headers": "HTTP/1.1 200 OK\r\nServer: uServer\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n",
    "timestamp": "1415926535.898",
    "body": ""
}

request_header3 = {
    "headers":
        "POST / HTTP/1.1\r\nHost: www.yetanotherexample.com\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 11\r\n\r\n",
    "timestamp": "1415926535.898",
    "body": "knock knock"
}
response_header3 = {
    "headers": "HTTP/1.1 200 OK\r\nServer: uServer\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n",
    "timestamp": "1415926535.898",
    "body": ""
}

server.addResponse("sessionlog.json", request_header, response_header)
server2.addResponse("sessionlog.json", request_header2, response_header2)
server3.addResponse("sessionlog.json", request_header3, response_header3)

# add ssl materials like key, certificates for the server
ts.addDefaultSSLFiles()

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
    })

ts.Disk.remap_config.AddLine('map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port))
ts.Disk.remap_config.AddLine('map http://www.yetanotherexample.com http://127.0.0.1:{0}'.format(server3.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map https://www.anotherexample.com https://127.0.0.1:{0}'.format(server2.Variables.SSL_Port, ts.Variables.ssl_port))
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(Test.Variables.upstream_port))

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

# smuggle-client is built via `make`. Here we copy the built binary down to the
# test directory so that the test runs in this file can use it.
Test.Setup.Copy(os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'chunked_encoding', 'smuggle-client'))

# HTTP1.1 GET: www.example.com
tr = Test.AddTestRun()
tr.TimeOut = 5
tr.Processes.Default.Command = 'curl --http1.1 --proxy 127.0.0.1:{0} http://www.example.com  --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(server2)
tr.Processes.Default.StartBefore(server3)
# Delay on readiness of our ssl ports
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr = "gold/chunked_GET_200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# HTTP2 POST: www.example.com Host, chunked body
tr = Test.AddTestRun()
tr.TimeOut = 5
tr.Processes.Default.Command = 'curl --http2 -k https://127.0.0.1:{0} --verbose -H "Host: www.anotherexample.com" -d "Knock knock"'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/h2_chunked_POST_200.gold"

# HTTP1.1 POST: www.yetanotherexample.com Host, explicit size
tr = Test.AddTestRun()
tr.TimeOut = 5
tr.Processes.Default.Command = 'curl http://127.0.0.1:{0} -H "Host: www.yetanotherexample.com" --verbose -d "knock knock"'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/chunked_POST_200.gold"
tr.StillRunningAfter = server

# HTTP1.1 POST: www.example.com Host, chunked body
tr = Test.AddTestRun()
tr.TimeOut = 5
tr.Processes.Default.Command = 'curl http://127.0.0.1:{0} -H "Host: www.yetanotherexample.com" --verbose -H "Transfer-Encoding: chunked" -d "Knock knock"'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/chunked_POST_200.gold"
tr.StillRunningAfter = server

server4_out = Test.Disk.File("outserver4")
server4_out.Content = Testers.ExcludesExpression("sneaky", "Extra body bytes should not be delivered")

# HTTP/1.1 Try to smuggle another request to the origin
tr = Test.AddTestRun()
tr.TimeOut = 5
tr.Setup.Copy('server4.sh')
tr.Setup.Copy('case4.sh')
tr.Processes.Default.Command = 'sh ./case4.sh {0} {1}'.format(ts.Variables.ssl_port, Test.Variables.upstream_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("content-length:", "Response should not include content length")
# Transfer encoding to origin, but no content-length
# No extra bytes in body seen by origin


class TestChunkedTrailers:
    """Verify chunked trailer proxy behavior."""

    _chunked_dropped_replay: str = "replays/chunked_trailer_dropped.replay.yaml"
    _proxied_dropped_replay: str = "replays/chunked_trailer_proxied.replay.yaml"

    def __init__(self, configure_drop_trailers: bool):
        """Create a test to verify chunked trailer behavior.

        :param configure_drop_trailers: Whether to configure ATS to drop
        trailers or not.
        """
        self._configure_drop_trailers = configure_drop_trailers
        self._replay_file = self._chunked_dropped_replay if configure_drop_trailers else self._proxied_dropped_replay
        behavior_description = "drop" if configure_drop_trailers else "proxy"
        tr = Test.AddTestRun(f'Verify chunked tailers behavior: {behavior_description}')
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_ts(tr)
        self._configure_client(tr)

    def _configure_dns(self, tr: 'TestRun') -> "Process":
        """Configure DNS for the test run.

        :param tr: The TestRun to configure DNS for.
        :return: The DNS process.
        """
        name = 'dns-drop-trailers' if self._configure_drop_trailers else 'dns-proxy-trailers'
        self._dns = tr.MakeDNServer(name, default='127.0.0.1')
        return self._dns

    def _configure_server(self, tr: 'TestRun') -> 'Process':
        """Configure the origin server for the test run.

        :param tr: The TestRun to configure the server for.
        :return: The origin server process.
        """
        name = 'server-drop-trailers' if self._configure_drop_trailers else 'server-proxy-trailers'
        self._server = tr.AddVerifierServerProcess(name, self._replay_file)
        if self._configure_drop_trailers:
            self._server.Streams.All += Testers.ExcludesExpression('Client: ATS', 'Verify the Client trailer was dropped.')
            self._server.Streams.All += Testers.ExcludesExpression('ETag: "abc"', 'Verify the ETag trailer was dropped.')
        else:
            self._server.Streams.All += Testers.ContainsExpression('Client: ATS', 'Verify the Client trailer was proxied.')
            self._server.Streams.All += Testers.ContainsExpression('ETag: "abc"', 'Verify the ETag trailer was proxied.')
        return self._server

    def _configure_ts(self, tr: 'TestRun') -> 'Process':
        """Configure ATS for the test run.

        :param tr: The TestRun to configure ATS for.
        :return: The ATS process.
        """
        name = 'ts-drop-trailers' if self._configure_drop_trailers else 'ts-proxy-trailers'
        ts = tr.MakeATSProcess(name, enable_cache=False)
        self._ts = ts
        port = self._server.Variables.http_port
        ts.Disk.remap_config.AddLine(f'map / http://backend.example.com:{port}/')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL'
            })
        if self._configure_drop_trailers:
            ts.Disk.records_config.update({
                'proxy.config.http.drop_chunked_trailers': 1,
            })
        return ts

    def _configure_client(self, tr: 'TestRun') -> 'Process':
        """Configure the client for the test run.

        :param tr: The TestRun to configure the client for.
        :return: The client process.
        """
        name = 'client-drop-trailers' if self._configure_drop_trailers else 'client-proxy-trailers'
        self._client = tr.AddVerifierClientProcess(name, self._replay_file, http_ports=[self._ts.Variables.port])
        self._client.StartBefore(self._dns)
        self._client.StartBefore(self._server)
        self._client.StartBefore(self._ts)

        if self._configure_drop_trailers:
            self._client.Streams.All += Testers.ExcludesExpression('Sever: ATS', 'Verify the Server trailer was dropped.')
            self._client.Streams.All += Testers.ExcludesExpression('ETag: "def"', 'Verify the ETag trailer was dropped.')
        else:
            self._client.Streams.All += Testers.ContainsExpression('Sever: ATS', 'Verify the Server trailer was proxied.')
            self._client.Streams.All += Testers.ContainsExpression('ETag: "def"', 'Verify the ETag trailer was proxied.')
        return self._client


TestChunkedTrailers(configure_drop_trailers=True)
TestChunkedTrailers(configure_drop_trailers=False)
