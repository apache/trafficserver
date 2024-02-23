'''
Verify ATS parent.config dest_ip dns bug fix.
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
Verify ATS parent_config with dest_ip=...
'''

# Create origin
origin = Test.MakeOriginServer("origin")

# default root
request_header_chk = {
    "headers": "GET / HTTP/1.1\r\n" + "Host: ats\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

response_header_chk = {
    "headers": "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

origin.addResponse("sessionlog.json", request_header_chk, response_header_chk)

request_header = {
    "headers":
        f"GET /foo.txt HTTP/1.1\r\n"
        "Host: does.not.matter\r\n"  # But cant be omitted
        "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

response_header = {
    "headers": "HTTP/1.1 200 OK\r\n"
               "Connection: close\r\n"
               "Cache-control: max-age=60\r\n"
               "\r\n",
    "timestamp": "1469733493.993",
    "body": f"This is the body for foo.txt\n"
}
origin.addResponse("sessionlog.json", request_header, response_header)

# Configure DNS for cache layering
dns = Test.MakeDNServer("dns")
dns.addRecords(records={f"origin": ["127.0.0.1"]})
dns.addRecords(records={f"ts1": ["127.0.0.1"]})
dns.addRecords(records={f"ts0": ["127.0.0.1"]})
dns.addRecords(records={f"foo.bar": ["142.250.72.14"]})  # google.com

# Configure Traffic Server Mid
ts1 = Test.MakeATSProcess("ts1")
ts1.Disk.remap_config.AddLine(f"map / http://origin:{origin.Variables.Port}")

ts1.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|dns|hostdb|parent',
        'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
        'proxy.config.dns.resolv_conf': "NULL",
        'proxy.config.hostdb.lookup_timeout': 2,
        'proxy.config.http.connect_attempts_timeout': 1,
        'proxy.config.http.parent_proxy.self_detect': 0,
        'proxy.config.http.insert_response_via_str': 1,
        'proxy.config.proxy_name': 'ts1',
    })

# Configure Traffic Server Edge
ts0 = Test.MakeATSProcess("ts0")
ts0.Disk.remap_config.AddLine("map http://foo.bar http://foo.bar")

ts0.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|dns|hostdb|parent',
        'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
        'proxy.config.dns.resolv_conf': "NULL",
        'proxy.config.hostdb.lookup_timeout': 2,
        'proxy.config.http.connect_attempts_timeout': 1,
        'proxy.config.http.parent_proxy.self_detect': 0,
        'proxy.config.http.insert_response_via_str': 1,
        'proxy.config.proxy_name': 'ts0',
    })

ts0.Disk.parent_config.AddLines(
    [
        f"dest_ip=93.184.216.34 port=80 go_direct=true",  # example.com
        f'dest_host=foo.bar port=80 parent="ts1:{ts1.Variables.port}|1;" go_direct="false" parent_is_proxy="true"',
    ])

# Start everything up
tr = Test.AddTestRun("init")
tr.Processes.Default.StartBefore(origin)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(ts0)
tr.Processes.Default.StartBefore(ts1)
tr.Processes.Default.Command = 'echo start TS, TSH_N, HTTP origin and DNS.'
tr.Processes.Default.ReturnCode = 0

curl_and_args = f"curl -s -D /dev/stdout -o /dev/stderr -x http://127.0.0.1:{ts0.Variables.port}"

# Request asset that goes through the layers
tr = Test.AddTestRun("request")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://foo.bar/foo.txt'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("Via:.* ts1 .* ts0 ", "expected via header")
tr.StillRunningAfter = ts0
