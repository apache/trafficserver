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

import sys

Test.Summary = '''
Test tunneling based on SNI
'''

# Define default ATS
ts = Test.MakeATSProcess("ts", enable_tls=True, enable_proxy_protocol=True)
server_foo = Test.MakeOriginServer("server_foo", ssl=True)
server_bar = Test.MakeOriginServer("server_bar", ssl=True)
server2 = Test.MakeOriginServer("server2")
# The following server will listen on a port that is not in the connect_ports
# list.
server_forbidden = Test.MakeOriginServer("forbidden", ssl=True)
dns = Test.MakeDNServer("dns")

request_foo_header = {"headers": "GET / HTTP/1.1\r\nHost: foo.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
request_bar_header = {"headers": "GET / HTTP/1.1\r\nHost: bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
request_pp_header = {
    "headers": "GET /proxy_protocol HTTP/1.1\r\nHost: proxy.protocol.port.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_foo_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "foo ok"}
response_bar_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "bar ok"}
response_pp_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "pp ok"}
server_foo.addResponse("sessionlog.json", request_foo_header, response_foo_header)
server_foo.addResponse("sessionlog.json", request_pp_header, response_pp_header)
server_bar.addResponse("sessionlog.json", request_bar_header, response_bar_header)
server_forbidden.addResponse("sessionlog.json", request_pp_header, response_pp_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/signed-bar.pem")
ts.addSSLfile("ssl/signed-bar.key")
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")
ts.addSSLfile("ssl/signer.key")

dns.addRecords(records={"localhost": ["127.0.0.1"]})
dns.addRecords(records={"one.testmatch": ["127.0.0.1"]})
dns.addRecords(records={"two.example.one": ["127.0.0.1"]})
dns.addRecords(records={"backend.incoming.port.com": ["127.0.0.1"]})
dns.addRecords(records={"backend.proxy.protocol.port.com": ["127.0.0.1"]})
dns.addRecords(records={"backend.wildcard.with.incoming.port.com": ["127.0.0.1"]})
dns.addRecords(records={"backend.wildcard.with.proxy.protocol.port.com": ["127.0.0.1"]})
# Need no remap rules.  Everything should be processed by sni

# Make sure the TS server certs are different from the origin certs
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=signed-foo.pem ssl_key_name=signed-foo.key')

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.http.connect_ports':
            '{0} {1} {2}'.format(ts.Variables.ssl_port, server_foo.Variables.SSL_Port, server_bar.Variables.SSL_Port),
        'proxy.config.ssl.client.CA.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.client.CA.cert.filename': 'signer.pem',
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.url_remap.pristine_host_hdr': 1,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|ssl_sni|proxyprotocol',
        'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
        'proxy.config.dns.resolv_conf': 'NULL'
    })

# foo.com should not terminate.  Just tunnel to server_foo
# bar.com should terminate.  Forward its tcp stream to server_bar
# empty SNI should tunnel to server_bar
ts.Disk.sni_yaml.AddLines(
    [
        'sni:',
        '- fqdn: foo.com',
        "  tunnel_route: localhost:{0}".format(server_foo.Variables.SSL_Port),
        "- fqdn: '*.bar.com'",
        "  tunnel_route: localhost:{0}".format(server_foo.Variables.SSL_Port),
        "- fqdn: '*.match.com'",
        "  tunnel_route: $1.testmatch:{0}".format(server_foo.Variables.SSL_Port),
        "- fqdn: '*.ok.two.com'",
        "  tunnel_route: two.example.$1:{0}".format(server_foo.Variables.SSL_Port),
        "- fqdn: ''",  # No SNI sent
        "  tunnel_route: localhost:{0}".format(server_bar.Variables.SSL_Port),
        "- fqdn: 'incoming.port.com'",
        "  tunnel_route: backend.incoming.port.com:{inbound_local_port}",
        "- fqdn: 'proxy.protocol.port.com'",
        "  tunnel_route: backend.proxy.protocol.port.com:{proxy_protocol_port}",
        "- fqdn: '*.backend.incoming.port.com'",
        "  tunnel_route: backend.$1.incoming.port.com:{inbound_local_port}",
        "- fqdn: '*.with.incoming.port.com'",
        "  tunnel_route: backend.$1.with.incoming.port.com:{inbound_local_port}",
        "- fqdn: '*.with.proxy.protocol.port.com'",
        "  tunnel_route: backend.$1.with.proxy.protocol.port.com:{proxy_protocol_port}",
    ])

tr = Test.AddTestRun("foo.com Tunnel-test")
tr.MakeCurlCommand("-v --resolve 'foo.com:{0}:127.0.0.1' -k  https://foo.com:{0}".format(ts.Variables.ssl_port))
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server_foo)
tr.Processes.Default.StartBefore(server_bar)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server_foo
tr.StillRunningAfter = server_bar
tr.StillRunningAfter = dns
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
    "Not Found on Accelerato", "Should not try to remap on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Should not TLS terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Should get a successful response")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("ATS", "Do not terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("foo ok", "Should get a response from bar")

tr = Test.AddTestRun("bob.bar.com Tunnel-test")
tr.MakeCurlCommand("-v --resolve 'bob.bar.com:{0}:127.0.0.1' -k  https://bob.bar.com:{0}".format(ts.Variables.ssl_port))
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
    "Not Found on Accelerato", "Should not try to remap on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Should not TLS terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Should get a successful response")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("ATS", "Do not terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("foo ok", "Should get a response from bar")

tr = Test.AddTestRun("bar.com no Tunnel-test")
tr.MakeCurlCommand("-v --resolve 'bar.com:{0}:127.0.0.1' -k  https://bar.com:{0}".format(ts.Variables.ssl_port))
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Not Found on Accelerato", "Terminates on on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("ATS", "Terminate on Traffic Server")

tr = Test.AddTestRun("no SNI Tunnel-test")
tr.MakeCurlCommand("-v -k  https://127.0.0.1:{0}".format(ts.Variables.ssl_port))
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
    "Not Found on Accelerato", "Should not try to remap on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Should get a successful response")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("ATS", "Do not terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("bar ok", "Should get a response from bar")

tr = Test.AddTestRun("one.match.com Tunnel-test")
tr.MakeCurlCommand("-vvv --resolve 'one.match.com:{0}:127.0.0.1' -k  https://one.match.com:{0}".format(ts.Variables.ssl_port))
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
    "Not Found on Accelerato", "Should not try to remap on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Should not TLS terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Should get a successful response")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("ATS", "Do not terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("foo ok", "Should get a response from tm")

tr = Test.AddTestRun("one.ok.two.com Tunnel-test")
tr.MakeCurlCommand("-vvv --resolve 'one.ok.two.com:{0}:127.0.0.1' -k  https://one.ok.two.com:{0}".format(ts.Variables.ssl_port))
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
    "Not Found on Accelerato", "Should not try to remap on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Should not TLS terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Should get a successful response")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("ATS", "Do not terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("foo ok", "Should get a response from tm")

tr = Test.AddTestRun("test {inbound_local_port}")
tr.MakeCurlCommand(
    "-vvv --resolve 'incoming.port.com:{0}:127.0.0.1' -k  https://incoming.port.com:{0}".format(ts.Variables.ssl_port))
# The tunnel connecting to the outgoing port which is the same as the incoming
# port (per the `inbound_local_port` configuration) will result in ATS
# connecting back to itself. This will result in a connection close and a
# non-zero return code from curl. In production, the server will listen on the
# same port as ATS but have a different IP.
tr.ReturnCode = 35
tr.StillRunningAfter = ts
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    f"CONNECT tunnel://backend.incoming.port.com:{ts.Variables.ssl_port} HTTP/1.1", "Verify a CONNECT request is handled")
ts.Disk.traffic_out.Content += Testers.ContainsExpression("HTTP/1.1 400 Cycle Detected", "The loop should be detected")

tr = Test.AddTestRun("test {proxy_protocol_port}")
tr.Setup.Copy('proxy_protocol_client.py')
tr.Processes.Default.Command = (
    f'{sys.executable} proxy_protocol_client.py '
    f'127.0.0.1 {ts.Variables.proxy_protocol_ssl_port} proxy.protocol.port.com '
    f'127.0.0.1 127.0.0.1 60123 {server_foo.Variables.SSL_Port} '
    f'2 --https')
tr.ReturnCode = 0
tr.TimeOut = 5
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Verify a successful response is received")

# This test run causes an exception in proxy_protocol_client.py
#
tr = Test.AddTestRun("test proxy_protocol_port - not in connect_ports")
tr.Processes.Default.StartBefore(server_forbidden)
tr.Setup.Copy('proxy_protocol_client.py')
# Note that server_forbidden is listing on a port that is not in the
# connect_ports list. Therefore the tunnel should be rejected.
rejected_port = server_forbidden.Variables.SSL_Port
tr.Processes.Default.Command = (
    f'{sys.executable} proxy_protocol_client.py '
    f'127.0.0.1 {ts.Variables.proxy_protocol_ssl_port} proxy.protocol.port.com '
    f'127.0.0.1 127.0.0.1 60123 {rejected_port} '
    f'2 --https')
tr.ReturnCode = 1
tr.TimeOut = 5
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("ssl.SSL.*Error:.*EOF", "Verify a the handshake failed")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    f"Rejected a tunnel to port {rejected_port} not in connect_ports", "Verify the tunnel was rejected")

tr = Test.AddTestRun("test wildcard with inbound_local_port")
tr.MakeCurlCommand(
    "-vvv --resolve 'wildcard.with.incoming.port.com:{0}:127.0.0.1' -k  https://wildcard.with.incoming.port.com:{0}".format(
        ts.Variables.ssl_port))

# See the inbound_local_port test above for the explanation of the return code.
tr.ReturnCode = 35
tr.StillRunningAfter = ts
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    rf"Destination now is \[backend.wildcard.with.incoming.port.com:{ts.Variables.ssl_port}\]",
    "Verify the tunnel destination is expanded correctly.")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    f"CONNECT tunnel://backend.wildcard.with.incoming.port.com:{ts.Variables.ssl_port} HTTP/1.1",
    "Verify a CONNECT request is handled")
ts.Disk.traffic_out.Content += Testers.ContainsExpression("HTTP/1.1 400 Cycle Detected", "The loop should be detected")

tr = Test.AddTestRun("test wildcard with proxy_protocol_port")
tr.Setup.Copy('proxy_protocol_client.py')
tr.Processes.Default.Command = (
    f'{sys.executable} proxy_protocol_client.py '
    f'127.0.0.1 {ts.Variables.proxy_protocol_ssl_port} wildcard.with.proxy.protocol.port.com '
    f'127.0.0.1 127.0.0.1 60123 {server_foo.Variables.SSL_Port} '
    f'2 --https')
tr.ReturnCode = 0
tr.TimeOut = 5
tr.StillRunningAfter = ts
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    rf"Destination now is \[backend.wildcard.with.proxy.protocol.port.com:{server_foo.Variables.SSL_Port}\]",
    "Verify the tunnel destination is expanded correctly.")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Verify a successful response is received")

# Update sni file and reload
tr = Test.AddTestRun("Update config files")
# Update the SNI config
snipath = ts.Disk.sni_yaml.AbsPath
recordspath = ts.Disk.records_config.AbsPath
tr.Disk.File(snipath, id="sni_yaml", typename="ats:config"),
tr.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: bar.com',
    '  tunnel_route: localhost:{0}'.format(server_bar.Variables.SSL_Port),
])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server_foo
tr.StillRunningAfter = server_bar
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'echo Updated configs'
tr.Processes.Default.ReturnCode = 0

trreload = Test.AddTestRun("Reload config")
trreload.StillRunningAfter = ts
trreload.StillRunningAfter = server_foo
trreload.StillRunningAfter = server_bar
trreload.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
trreload.Processes.Default.Env = ts.Env
trreload.Processes.Default.ReturnCode = 0

# Should terminate on traffic_server (not tunnel)
tr = Test.AddTestRun("foo.com no Tunnel-test")
tr.StillRunningAfter = ts
# Wait for the reload to complete by running the sni_reload_done test
tr.Processes.Default.StartBefore(server2, ready=When.FileContains(ts.Disk.diags_log.Name, 'sni.yaml finished loading', 2))
tr.MakeCurlCommand("-v --resolve 'foo.com:{0}:127.0.0.1' -k  https://foo.com:{0}".format(ts.Variables.ssl_port))
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Not Found on Accelerato", "Terminates on on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("ATS", "Terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.TimeOut = 30

# Should tunnel to server_bar
tr = Test.AddTestRun("bar.com  Tunnel-test")
tr.MakeCurlCommand("-v --resolve 'bar.com:{0}:127.0.0.1' -k  https://bar.com:{0}".format(ts.Variables.ssl_port))
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Not Found on Accelerato", "Terminates on on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("ATS", "Terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("bar ok", "Should get a response from bar")

tr = Test.AddTestRun("Test Metrics")
tr.Processes.Default.Command = (
    f"{Test.Variables.AtsTestToolsDir}/stdout_wait" + " 'traffic_ctl metric get" +
    " proxy.process.http.total_incoming_connections" + " proxy.process.http.total_client_connections" +
    " proxy.process.http.total_client_connections_ipv4" + " proxy.process.http.total_client_connections_ipv6" +
    " proxy.process.http.total_server_connections" + " proxy.process.http2.total_client_connections" +
    " proxy.process.http.connect_requests" + " proxy.process.tunnel.total_client_connections_blind_tcp" +
    " proxy.process.tunnel.current_client_connections_blind_tcp" + " proxy.process.tunnel.total_server_connections_blind_tcp" +
    " proxy.process.tunnel.current_server_connections_blind_tcp" + " proxy.process.tunnel.total_client_connections_tls_tunnel" +
    " proxy.process.tunnel.current_client_connections_tls_tunnel" + " proxy.process.tunnel.total_client_connections_tls_forward" +
    " proxy.process.tunnel.current_client_connections_tls_forward" +
    " proxy.process.tunnel.total_client_connections_tls_partial_blind" +
    " proxy.process.tunnel.current_client_connections_tls_partial_blind" +
    " proxy.process.tunnel.total_client_connections_tls_http" + " proxy.process.tunnel.current_client_connections_tls_http" +
    " proxy.process.tunnel.total_server_connections_tls" + " proxy.process.tunnel.current_server_connections_tls'" +
    f" {Test.TestDirectory}/gold/tls-tunnel-metrics.gold")
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
