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
Test prefetch.so plugin (tiered mode).
'''

origin = Test.MakeOriginServer("origin")
for ind in range(2):
    filename = f"demo-{ind}.txt"
    request_header = {
        "headers":
            f"GET /test/{filename} HTTP/1.1\r\n" +
            "Host: does.not.matter\r\n" +  # But cannot be omitted.
            "\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }
    response_header = {
        "headers":
            "HTTP/1.1 200 OK\r\n" +
            "Connection: close\r\n" +
            "Cache-control: max-age=85000\r\n" +
            "\r\n",
        "timestamp": "1469733493.993",
        "body": f"This is the body for {filename}.\n"
    }
    origin.addResponse("sessionlog.json", request_header, response_header)

# allows for multiple ats on localhost
dns = Test.MakeDNServer("dns")

# next hop trafficserver instance
ts1 = Test.MakeATSProcess("ts1")
ts1.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'prefetch|http',
    'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
    'proxy.config.dns.resolv_conf': "NULL",
    'proxy.config.http.parent_proxy.self_detect': 0,
})
dns.addRecords(records={f"ts1": ["127.0.0.1"]})
ts1.Disk.remap_config.AddLine(
    f"map / http://127.0.0.1:{origin.Variables.Port}" +
    " @plugin=cachekey.so @pparam==--remove-all-params=true"
    " @plugin=prefetch.so @pparam==--front=false"
)

ts1.Disk.logging_yaml.AddLines(
    '''
logging:
 formats:
  - name: custom
    format: '%<cqup> %<pssc> %<crc> %<cwr> %<pscl> %<{X-CDN-Prefetch}cqh>'
 logs:
  - filename: transaction
    format: custom
'''.split("\n")
)

ts0 = Test.MakeATSProcess("ts0")
ts0.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'prefetch|http',
    'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
    'proxy.config.dns.resolv_conf': "NULL",
    'proxy.config.http.parent_proxy.self_detect': 0,
})

dns.addRecords(records={f"ts0": ["127.0.0.1"]})
ts0.Disk.remap_config.AddLine(
    f"map http://ts0 http://ts1:{ts1.Variables.port}" +
    " @plugin=cachekey.so @pparam=--remove-all-params=true"
    " @plugin=prefetch.so" +
    " @pparam=--front=true" +
    " @pparam=--fetch-policy=simple" +
    r" @pparam=--fetch-path-pattern=/(.*-)(\d+)(.*)/$1{$2+1}$3/" +
    "@pparam=--fetch-count=1"
)

ts0.Disk.logging_yaml.AddLines(
    '''
logging:
 formats:
  - name: custom
    format: '%<cqup> %<pssc> %<crc> %<cwr> %<pscl> %<{X-CDN-Prefetch}cqh>'
 logs:
  - filename: transaction
    format: custom
'''.split("\n")
)


# start everything up
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(origin)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(ts0)
tr.Processes.Default.StartBefore(ts1)
tr.Processes.Default.Command = 'echo start TS, TSH_N, HTTP origin and DNS.'
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    f'curl --verbose --proxy 127.0.0.1:{ts0.Variables.port} http://ts0/test/demo-0.txt'
)
tr.Processes.Default.ReturnCode = 0

condwaitpath = os.path.join(Test.Variables.AtsTestToolsDir, 'condwait')

# look for ts transaction log
ts0log = os.path.join(ts0.Variables.LOGDIR, 'transaction.log')
tr = Test.AddTestRun()
ps = tr.Processes.Default
ps.Command = (
    condwaitpath + ' 60 1 -f ' + ts0log
)

# look for ts1 transaction log
ts1log = os.path.join(ts1.Variables.LOGDIR, 'transaction.log')
tr = Test.AddTestRun()
ps = tr.Processes.Default
ps.Command = (
    condwaitpath + ' 60 1 -f ' + ts1log
)

tr.Processes.Default.Command = (
    f"cat {ts0log}"
)
tr.Streams.stdout = "prefetch_fb0.gold"

tr.Processes.Default.Command = (
    f"cat {ts1log}"
)
tr.Streams.stdout = "prefetch_fb1.gold"
