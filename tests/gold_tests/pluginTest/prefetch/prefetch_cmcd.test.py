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
import urllib.parse

Test.Summary = '''
Test prefetch.so plugin (simple mode).
'''

origin = Test.MakeOriginServer("origin")

asset_name = 'request.txt'
pf_name = 'prefetch.txt'
pf_header = f'Cmcd-Request: foo=12,nor="{pf_name}",bar=42'

request_header = {
    "headers":
    f"GET /tests/{asset_name} HTTP/1.1\r\n"
    "Host: does.not.matter\r\n"  # But cant be omitted
    f"{pf_header}\r\n"
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers":
    "HTTP/1.1 200 OK\r\n"
    "Connection: close\r\n"
    "Cache-control: max-age=60\r\n"
    "\r\n",
    "timestamp": "1469733493.993",
    "body": f"This is the body for {asset_name}\n"
}
origin.addResponse("sessionlog.json", request_header, response_header)

# query string
query_name = 'query?this=foo&that'
query_pf_name = 'query?bar=baz'
query_pf_header = f'Cmcd-Request: nor="{query_pf_name}"'

# nor field may be percent encoded
query_pf_perc_name = urllib.parse.quote(query_pf_name)
query_pf_perc_header = f'Cmcd-Request: nor="{query_pf_perc_name}"'

request_header = {
    "headers":
    f"GET /tests/{query_name} HTTP/1.1\r\n"
    "Host: does.not.matter\r\n"  # But cant be omitted
    f"{query_pf_perc_header}\r\n"
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers":
    "HTTP/1.1 200 OK\r\n"
    "Connection: close\r\n"
    "Cache-control: max-age=60\r\n"
    "\r\n",
    "timestamp": "1469733493.993",
    "body": f"This is the body for {query_name}\n"
}
origin.addResponse("sessionlog.json", request_header, response_header)

# setup the prefetched assets
names = [pf_name, query_pf_name]

for name in names:
    request_header = {
        "headers":
        f"GET /tests/{name} HTTP/1.1\r\n"
        "Host: does.not.matter\r\n"  # But cant be omitted
        "\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }
    response_header = {
        "headers":
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "Cache-control: max-age=60\r\n"
        "\r\n",
        "timestamp": "1469733493.993",
        "body": f"This is the body for {name}\n"
    }
    origin.addResponse("sessionlog.json", request_header, response_header)

# prefetch from root
root_name = 'root.txt'
root_header = f'Cmcd-Request: nor="rooted"'

request_header = {
    "headers":
    f"GET /{root_name} HTTP/1.1\r\n"
    "Host: does.not.matter\r\n"  # But cant be omitted
    f"{root_header}\r\n"
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers":
    "HTTP/1.1 200 OK\r\n"
    "Connection: close\r\n"
    "Cache-control: max-age=60\r\n"
    "\r\n",
    "timestamp": "1469733493.993",
    "body": f"This is the body for {root_name}\n"
}
origin.addResponse("sessionlog.json", request_header, response_header)

request_header = {
    "headers":
    f"GET /rooted HTTP/1.1\r\n"
    "Host: does.not.matter\r\n"  # But cant be omitted
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers":
    "HTTP/1.1 200 OK\r\n"
    "Connection: close\r\n"
    "Cache-control: max-age=60\r\n"
    "\r\n",
    "timestamp": "1469733493.993",
    "body": f"This is the body for rooted\n"
}
origin.addResponse("sessionlog.json", request_header, response_header)

# ignore if cmcd-request nrr= found
crr_name = 'crr.txt'
crr_header = f'Cmcd-Request: foo=12,nor="{crr_name}",bar=42,nrr="0-"'
request_header = {
    "headers":
    f"GET /tests/{crr_name} HTTP/1.1\r\n"
    "Host: does.not.matter\r\n"  # But cant be omitted
    f"{crr_header}\r\n"
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers":
    "HTTP/1.1 200 OK\r\n"
    "Connection: close\r\n"
    "Cache-control: max-age=60\r\n"
    "\r\n",
    "timestamp": "1469733493.993",
    "body": f"This is the body for {crr_name}\n"
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
    " @plugin=cachekey.so @pparam==--sort-params=true"
    " @plugin=prefetch.so @pparam==--front=false"
)

ts1.Disk.logging_yaml.AddLines(
    '''
logging:
 formats:
  - name: custom
    format: '%<cquuc> %<pssc> %<crc> %<cwr> %<pscl> %<{X-CDN-Prefetch}cqh>'
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
    " @plugin=cachekey.so @pparam=--sort-params=true"
    " @plugin=prefetch.so" +
    " @pparam=--front=true" +
    " @pparam=--fetch-policy=simple" +
    " @pparam=--cmcd-nor=true"
)

ts0.Disk.logging_yaml.AddLines(
    '''
logging:
 formats:
  - name: custom
    format: '%<cquuc> %<pssc> %<crc> %<cwr> %<pscl> %<{X-CDN-Prefetch}cqh>'
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

# attempt to get normal asset
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    f"curl --verbose --proxy 127.0.0.1:{ts0.Variables.port} http://ts0/tests/{asset_name}"
)
tr.Processes.Default.ReturnCode = 0

# issue curl form same asset, with prefetch
tr = Test.AddTestRun()
tr.DelayStart = 1
tr.Processes.Default.Command = (
    f"curl --verbose --proxy 127.0.0.1:{ts0.Variables.port} http://ts0/tests/{asset_name} -H \'{pf_header}\'"
)
tr.Processes.Default.ReturnCode = 0

# fetch the prefetched asset (only cached on ts1)
tr = Test.AddTestRun()
tr.DelayStart = 1
tr.Processes.Default.Command = (
    f"curl --verbose --proxy 127.0.0.1:{ts0.Variables.port} http://ts0/tests/{pf_name}"
)
tr.Processes.Default.ReturnCode = 0

# attempt to prefetch again
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    f"curl --verbose --proxy 127.0.0.1:{ts0.Variables.port} http://ts0/tests/{asset_name} -H \'{pf_header}\'"
)
tr.Processes.Default.ReturnCode = 0

# request the prefetched asset
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    f"curl --verbose --proxy 127.0.0.1:{ts0.Variables.port} http://ts0/tests/{pf_name}"
)
tr.Processes.Default.ReturnCode = 0

# prefetch using query params with query prefetch perc encoded
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    f"curl --verbose --proxy 127.0.0.1:{ts0.Variables.port} \'http://ts0/tests/{query_name}\' -H \'{query_pf_perc_header}\'"
)
tr.Processes.Default.ReturnCode = 0

# request the prefetched asset without perc encoding
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    f"curl --verbose --proxy 127.0.0.1:{ts0.Variables.port} \'http://ts0/tests/{query_pf_name}\'"
)
tr.Processes.Default.ReturnCode = 0

# ensure root path prefetch works
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    f"curl --verbose --proxy 127.0.0.1:{ts0.Variables.port} \'http://ts0/{root_name}\' -H \'{root_header}\'"
)
tr.Processes.Default.ReturnCode = 0

# ensure request with nrr= field is skipped
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    f"curl --verbose --proxy 127.0.0.1:{ts0.Variables.port} \'http://ts0/{crr_name}\' -H \'{crr_header}\'"
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

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    f"cat {ts0log}"
)
tr.Streams.stdout = "prefetch_cmcd0.gold"
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    f"cat {ts1log}"
)
tr.Streams.stdout = "prefetch_cmcd1.gold"
tr.Processes.Default.ReturnCode = 0
