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

Test.Summary = 'Testing ATS TCP handshake timeout'

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server", delay=15)
dns = Test.MakeDNServer("dns", ip='127.0.0.1', default=['127.0.0.1'])

request_header = {"headers": "GET /file HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "5678", "body": ""}

server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
    'proxy.config.dns.resolv_conf': 'NULL',
    'proxy.config.url_remap.remap_required': 0,
    'proxy.config.http.connect_attempts_timeout': 5
})

tr = Test.AddTestRun("tr")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts, ready=When.PortOpen(ts.Variables.port))
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.Command = 'curl -i -x http://127.0.0.1:{0} http://127.0.0.1:{1}/file'.format(
    ts.Variables.port, server.Variables.Port)
tr.Processes.Default.Streams.stdout = "timeout.gold"
