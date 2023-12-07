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

Test.Summary = '''
Test prefetch.so plugin (simple mode).
'''

server = Test.MakeOriginServer("server")
for i in range(4):
    request_header = {
        "headers":
            f"GET /texts/demo-{i + 1}.txt HTTP/1.1\r\n"
            "Host: does.not.matter\r\n"  # But cannot be omitted.
            "\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }
    response_header = {
        "headers": "HTTP/1.1 200 OK\r\n"
                   "Connection: close\r\n"
                   "Cache-control: max-age=85000\r\n"
                   "\r\n",
        "timestamp": "1469733493.993",
        "body": f"This is the body for demo-{i + 1}.txt.\n"
    }
    server.addResponse("sessionlog.json", request_header, response_header)

dns = Test.MakeDNServer("dns")

ts = Test.MakeATSProcess("ts", use_traffic_out=False, command="traffic_server 2> trace.log")
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|dns|prefetch',
        'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
        'proxy.config.dns.resolv_conf': "NULL",
    })
ts.Disk.remap_config.AddLine(
    f"map http://domain.in http://127.0.0.1:{server.Variables.Port}" + " @plugin=cachekey.so @pparam=--remove-all-params=true"
    " @plugin=prefetch.so" + " @pparam=--front=true" + " @pparam=--fetch-policy=simple" +
    r" @pparam=--fetch-path-pattern=/(.*-)(\d+)(.*)/$1{$2+1}$3/" + " @pparam=--fetch-count=3")

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = 'echo start TS, HTTP server and DNS.'
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (f'curl --verbose --proxy 127.0.0.1:{ts.Variables.port} http://domain.in/texts/demo-1.txt')
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = ("grep 'GET http://domain.in' trace.log")
tr.Streams.stdout = "prefetch_simple.gold"
tr.Processes.Default.ReturnCode = 0
