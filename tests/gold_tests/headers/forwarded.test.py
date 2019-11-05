'''
Test the Forwarded header and related configuration..
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
Test FORWARDED header.
'''

Test.SkipUnless(
    Condition.HasCurlFeature('http2'),
    Condition.HasCurlFeature('IPv6'),
)
Test.ContinueOnFail = True

testName = "FORWARDED"

server = Test.MakeOriginServer("server", options={'--load': os.path.join(Test.TestDirectory, 'forwarded-observer.py')})

request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.no-oride.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.forwarded-none.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.forwarded-for.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.forwarded-by-ip.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.forwarded-by-unknown.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.forwarded-by-server-name.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.forwarded-by-uuid.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.forwarded-proto.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.forwarded-host.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.forwarded-connection-compact.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.forwarded-connection-std.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.forwarded-connection-full.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# Set up to check the output after the tests have run.
#
forwarded_log_id = Test.Disk.File("forwarded.log")
forwarded_log_id.Content = "forwarded.gold"


def baselineTsSetup(ts, sslPort):

    ts.addSSLfile("../remap/ssl/server.pem")
    ts.addSSLfile("../remap/ssl/server.key")

    ts.Variables.ssl_port = sslPort

    ts.Disk.records_config.update({
        # 'proxy.config.diags.debug.enabled': 1,
        'proxy.config.url_remap.pristine_host_hdr': 1,  # Retain Host header in original incoming client request.
        'proxy.config.http.cache.http': 0,  # Make sure each request is forwarded to the origin server.
        'proxy.config.proxy_name': 'Poxy_Proxy',  # This will be the server name.
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.http.server_ports': (
            'ipv4:{0} ipv4:{1}:proto=http2;http:ssl ipv6:{0} ipv6:{1}:proto=http2;http:ssl'
            .format(ts.Variables.port, ts.Variables.ssl_port))
    })

    ts.Disk.ssl_multicert_config.AddLine(
        'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
    )

    ts.Disk.remap_config.AddLine(
        'map http://www.no-oride.com http://127.0.0.1:{0}'.format(server.Variables.Port)
    )


ts = Test.MakeATSProcess("ts", select_ports=False)

baselineTsSetup(ts, 4443)

ts.Disk.remap_config.AddLine(
    'map http://www.forwarded-none.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded=none'
)
ts.Disk.remap_config.AddLine(
    'map http://www.forwarded-for.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded=for'
)
ts.Disk.remap_config.AddLine(
    'map http://www.forwarded-by-ip.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded=by=ip'
)
ts.Disk.remap_config.AddLine(
    'map http://www.forwarded-by-unknown.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded=by=unknown'
)
ts.Disk.remap_config.AddLine(
    'map http://www.forwarded-by-server-name.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded=by=serverName'
)
ts.Disk.remap_config.AddLine(
    'map http://www.forwarded-by-uuid.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded=by=uuid'
)
ts.Disk.remap_config.AddLine(
    'map http://www.forwarded-proto.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded=proto'
)
ts.Disk.remap_config.AddLine(
    'map http://www.forwarded-host.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded=host'
)
ts.Disk.remap_config.AddLine(
    'map http://www.forwarded-connection-compact.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded=connection=compact'
)
ts.Disk.remap_config.AddLine(
    'map http://www.forwarded-connection-std.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded=connection=std'
)
ts.Disk.remap_config.AddLine(
    'map http://www.forwarded-connection-full.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded=connection=full'
)

# Basic HTTP 1.1 -- No Forwarded by default
tr = Test.AddTestRun()
# Wait for the micro server
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
# Delay on readiness of our ssl ports
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
#
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http1.1 --proxy localhost:{} http://www.no-oride.com'.format(ts.Variables.port)
)
tr.Processes.Default.ReturnCode = 0


def TestHttp1_1(host):

    tr = Test.AddTestRun()
    tr.Processes.Default.Command = (
        'curl --verbose --ipv4 --http1.1 --proxy localhost:{} http://{}'.format(ts.Variables.port, host)
    )
    tr.Processes.Default.ReturnCode = 0


# Basic HTTP 1.1 -- No Forwarded -- explicit configuration.
#
TestHttp1_1('www.forwarded-none.com')

# Test enabling of each forwarded parameter singly.

TestHttp1_1('www.forwarded-for.com')

# Note:  forwaded-obsersver.py counts on the "by" tests being done in the order below.

TestHttp1_1('www.forwarded-by-ip.com')
TestHttp1_1('www.forwarded-by-unknown.com')
TestHttp1_1('www.forwarded-by-server-name.com')
TestHttp1_1('www.forwarded-by-uuid.com')

TestHttp1_1('www.forwarded-proto.com')
TestHttp1_1('www.forwarded-host.com')
TestHttp1_1('www.forwarded-connection-compact.com')
TestHttp1_1('www.forwarded-connection-std.com')
TestHttp1_1('www.forwarded-connection-full.com')

ts2 = Test.MakeATSProcess("ts2", command="traffic_manager", select_ports=False)

ts2.Variables.port += 1

baselineTsSetup(ts2, 4444)

ts2.Disk.records_config.update({
    'proxy.config.url_remap.pristine_host_hdr': 1,  # Retain Host header in original incoming client request.
    'proxy.config.http.insert_forwarded': 'by=uuid'})

ts2.Disk.remap_config.AddLine(
    'map https://www.no-oride.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)

# Forwarded header with UUID of 2nd ATS.
tr = Test.AddTestRun()
# Delay on readiness of our ssl ports
tr.Processes.Default.StartBefore(Test.Processes.ts2, ready=When.PortOpen(ts2.Variables.ssl_port))
#
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http1.1 --proxy localhost:{} http://www.no-oride.com'.format(ts2.Variables.port)
)
tr.Processes.Default.ReturnCode = 0

# Call traffic_ctrl to set insert_forwarded
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'traffic_ctl --debug config set proxy.config.http.insert_forwarded' +
    ' "for|by=ip|by=unknown|by=servername|by=uuid|proto|host|connection=compact|connection=std|connection=full"'
)
tr.Processes.Default.ForceUseShell = False
tr.Processes.Default.Env = ts2.Env
tr.Processes.Default.ReturnCode = 0

# HTTP 1.1
tr = Test.AddTestRun()
# Delay to give traffic_ctl config change time to take effect.
tr.DelayStart = 15
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http1.1 --proxy localhost:{} http://www.no-oride.com'.format(ts2.Variables.port)
)
tr.Processes.Default.ReturnCode = 0

# HTTP 1.0
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http1.0 --proxy localhost:{} http://www.no-oride.com'.format(ts2.Variables.port)
)
tr.Processes.Default.ReturnCode = 0

# HTTP 1.0 -- Forwarded headers already present
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    "curl --verbose -H 'forwarded:for=0.6.6.6' -H 'forwarded:for=_argh' --ipv4 --http1.0" +
    " --proxy localhost:{} http://www.no-oride.com".format(ts2.Variables.port)
)
tr.Processes.Default.ReturnCode = 0

# HTTP 2
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http2 --insecure --header "Host: www.no-oride.com"' +
    ' https://localhost:{}'.format(ts2.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0

# TLS
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http1.1 --insecure --header "Host: www.no-oride.com" https://localhost:{}'
    .format(ts2.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0

# IPv6

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --verbose --ipv6 --http1.1 --proxy localhost:{} http://www.no-oride.com'.format(ts2.Variables.port)
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --verbose --ipv6 --http1.1 --insecure --header "Host: www.no-oride.com" https://localhost:{}'.format(
        ts2.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0
