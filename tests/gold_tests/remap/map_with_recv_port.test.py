'''
Verify correct behavior of regex_map in remap.config.
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
Verify correct behavior of map_with_recv_port in remap.config.
'''

Test.ContinueOnFail = True
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")
dns = Test.MakeDNServer("dns", default='127.0.0.1')

Test.testName = ""
request_header_ip = {"headers": "GET /ip HTTP/1.1\r\nHost: origin.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header_ip = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "ip"}
request_header_unix = {
    "headers": "GET /unix HTTP/1.1\r\nHost: origin.example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header_unix = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "unix"}
request_header_error = {
    "headers": "GET /error HTTP/1.1\r\nHost: origin.example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header_error = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "error"}
server.addResponse("sessionfile.log", request_header_ip, response_header_ip)
server.addResponse("sessionfile.log", request_header_unix, response_header_unix)
server.addResponse("sessionfile.log", request_header_error, response_header_error)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|dns',
        'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
        'proxy.config.dns.resolv_conf': 'NULL'
    })

# This map rule should not match
ts.Disk.remap_config.AddLine(
    r'map '
    r'http://test.example.com '
    r'http://origin.example.com:{}/error'.format(server.Variables.Port))
# Rule for IP interface
ts.Disk.remap_config.AddLine(
    r'map_with_recv_port '
    r'http://test.example.com:{}/ '
    r'http://origin.example.com:{}/ip'.format(ts.Variables.port, server.Variables.Port))
# Rule for Unix Domain Socket
ts.Disk.remap_config.AddLine(
    r'map_with_recv_port '
    r'http+unix://test.example.com '
    r'http://origin.example.com:{}/unix'.format(server.Variables.Port))

tr = Test.AddTestRun()
tr.MakeCurlCommand('-H"Host: test.example.com" http://127.0.0.1:{0}/ --verbose'.format(ts.Variables.port), ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(Test.Processes.ts)
if Condition.CurlUsingUnixDomainSocket():
    tr.Processes.Default.Streams.stderr = "gold/map-with-recv-port-unix.gold"
else:
    tr.Processes.Default.Streams.stderr = "gold/map-with-recv-port-ip.gold"
tr.StillRunningAfter = server
