'''
Verify ip_allow filtering behavior.
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
Verify ip_allow filtering behavior.
'''

Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts", command="traffic_manager", select_ports=True)
server = Test.MakeOriginServer("server", ssl=True)

testName = ""
request = {
        "headers":
        "GET /get HTTP/1.1\r\n"
        "Host: www.example.com:80\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""}
response = {
        "headers":
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 3\r\n"
        "Connection: close\r\n\r\n",
        "timestamp":
        "1469733493.993", "body": "xxx"}
server.addResponse("sessionlog.json", request, response)
request = {
        "headers":
        "CONNECT www.example.com:80/connect HTTP/1.1\r\n"
        "Host: www.example.com:80\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""}
response = {
        "headers":
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 3\r\n"
        "Connection: close\r\n\r\n",
        "timestamp":
        "1469733493.993", "body": "xxx"}
server.addResponse("sessionlog.json", request, response)

# ATS Configuration for TLS.
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.http.connect_ports': '{0} {1}'.format(ts.Variables.port, server.Variables.SSL_Port),
})

format_string = ('%<cqtd>-%<cqtt> %<stms> %<ttms> %<chi> %<crc>/%<pssc> %<psql> '
                 '%<cqhm> %<cquc> %<phr>/%<pqsn> %<psct> %<{Y-RID}pqh> '
                 '%<{Y-YPCS}pqh> %<{Host}cqh> %<{CHAD}pqh>  '
                 'sftover=%<{x-safet-overlimit-rules}cqh> sftmat=%<{x-safet-matched-rules}cqh> '
                 'sftcls=%<{x-safet-classification}cqh> '
                 'sftbadclf=%<{x-safet-bad-classifiers}cqh> yra=%<{Y-RA}cqh> scheme=%<cqus>')

ts.Disk.logging_config.AddLines(
    ''' squid_format_for_custom = format {{
  Format = '{}'
}}

log.ascii {{
        Format = squid_format_for_custom,
        Filename = 'squid.log',
}}'''.format(format_string).split("\n")
)

ts.Disk.remap_config.AddLine(
    'map / https://127.0.0.1:{0}'.format(server.Variables.SSL_Port)
)

# Note that CONNECT is not in the allowed list.
ts.Disk.ip_allow_config.AddLines(
     '''
# whitelist methods. Note that CONNECT is missing.
src_ip=0.0.0.0-255.255.255.255                    action=ip_allow  method=GET|HEAD|POST|PUT|DELETE|OPTIONS|PATCH
src_ip=::-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff action=ip_allow  method=GET|HEAD|POST|PUT|DELETE|OPTIONS|PATCH
# deny all other methods
src_ip=0.0.0.0-255.255.255.255                    action=ip_deny   method=ALL
src_ip=::-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff action=ip_deny   method=ALL
'''.split("\n")
)

#
# TEST 1: Perform a GET request. Should be allowed because GET is in the whitelist.
#
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.SSL_Port))
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.port))

tr.Processes.Default.Command = ('curl --verbose -H "Host: www.example.com" http://localhost:{ts_port}/get'.
                                format(ts_port=ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = 'gold/200.gold'
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

#
# TEST 2: Perform a CONNECT request. Should not be allowed because it's not in the whitelist.
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = ('curl --verbose -X CONNECT -H "Host: localhost" http://localhost:{ts_port}/connect'.
                                format(ts_port=ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = 'gold/403.gold'
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
