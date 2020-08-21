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

Test.Summary = '''
Test sslheaders plugin.
'''

Test.SkipUnless(
    Condition.HasCurlFeature('http2'),
)

Test.Disk.File('sslheaders.log').Content = 'sslheaders.gold'

server = Test.MakeOriginServer("server", options={'--load': Test.TestDirectory + '/observer.py'})

request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
# ts.addSSLfile("ssl/signer.pem")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.http.cache.http': 0,  # Make sure each request is forwarded to the origin server.
    'proxy.config.proxy_name': 'Poxy_Proxy',  # This will be the server name.
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.server_ports': (
        'ipv4:{0} ipv4:{1}:proto=http2;http:ssl ipv6:{0} ipv6:{1}:proto=http2;http:ssl'
        .format(ts.Variables.port, ts.Variables.ssl_port)),
    #    'proxy.config.ssl.client.verify.server':  0,
    #    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    #    'proxy.config.url_remap.pristine_host_hdr' : 1,
    #    'proxy.config.ssl.client.certification_level': 2,
    #    'proxy.config.ssl.CA.cert.filename': '{0}/signer.pem'.format(ts.Variables.SSLDir),
    #    'proxy.config.ssl.TLSv1_3': 0
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map http://bar.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    'map https://bar.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)

ts.Disk.plugin_config.AddLine(
    'sslheaders.so SSL-Client-ID=client.subject'
)

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = (
    'curl -H "SSL-Client-ID: My Fake Client ID" --verbose --ipv4 --insecure --header "Host: bar.com"' +
    ' https://localhost:{}'.format(ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0
