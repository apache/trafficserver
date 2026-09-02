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
Test a basic remap of a http connection
'''

Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")
server2 = Test.MakeOriginServer("server2", lookup_key="{%Host}{PATH}")
dns = Test.MakeDNServer("dns")

Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

request_header2 = {"headers": "GET /test HTTP/1.1\r\nHost: www.testexample.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header2 = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)
server2.addResponse("sessionfile.log", request_header2, response_header2)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http.*|dns|conf_remap|remap_yaml',
        'proxy.config.http.referer_filter': 1,
        'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
        'proxy.config.dns.resolv_conf': 'NULL'
    })

ts.Disk.remap_yaml.AddLines(
    f'''
remap:
  - type: map
    from:
      url: http://www.example.com
    to:
      url: http://127.0.0.1:{server.Variables.Port}
  - type: map_with_recv_port
    from:
      url: http://www.example2.com:{ts.Variables.port}
    to:
      url: http://127.0.0.1:{server.Variables.Port}
  - type: map
    from:
      url: http://www.example.com:8080
    to:
      url: http://127.0.0.1:{server.Variables.Port}
  - type: redirect
    from:
      url: http://test3.com
    to:
      url: http://httpbin.org
  - type: map_with_referer
    from:
      url: http://test4.com
    to:
      url: http://127.0.0.1:{server.Variables.Port}
    redirect:
      url: http://httpbin.org
      regex:
        - (.*[.])?persia[.]com
  - type: map
    from:
      url: http://testDNS.com
    to:
      url: http://audrey.hepburn.com:{server.Variables.Port}
  - type: map
    from:
      url: http://www.testexample.com
    to:
      url: http://127.0.0.1:{server2.Variables.Port}
    plugins:
      - name: conf_remap.so
        params:
          - proxy.config.url_remap.pristine_host_hdr=1
    '''.split("\n"))

dns.addRecords(records={"audrey.hepburn.com.": ["127.0.0.1"]})
dns.addRecords(records={"whatever.com.": ["127.0.0.1"]})

# call localhost straight
tr = Test.AddTestRun()
tr.MakeCurlCommand('"http://127.0.0.1:{0}/" --verbose'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr = "gold/remap-hitATS-404.gold"
tr.StillRunningAfter = server

# www.example.com host
tr = Test.AddTestRun()
tr.MakeCurlCommand(
    '--proxy 127.0.0.1:{0} "http://www.example.com"  -H "Proxy-Connection: keep-alive" --verbose'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-200.gold"

# www.example2.com host (match on receive port)
# map_with_recv_port doesn't work with UDS
if not Condition.CurlUsingUnixDomainSocket():
    tr = Test.AddTestRun()
    tr.MakeCurlCommand(
        '--proxy 127.0.0.1:{0} "http://www.example2.com"  -H "Proxy-Connection: keep-alive" --verbose'.format(ts.Variables.port),
        ts=ts)
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stderr = "gold/remap2-200.gold"

# www.example.com:80 host
tr = Test.AddTestRun()
tr.MakeCurlCommand(
    ' --proxy 127.0.0.1:{0} "http://www.example.com:80/"  -H "Proxy-Connection: keep-alive" --verbose'.format(ts.Variables.port),
    ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-200.gold"

# www.example.com:8080 host
tr = Test.AddTestRun()
tr.MakeCurlCommand(
    ' --proxy 127.0.0.1:{0} "http://www.example.com:8080"  -H "Proxy-Connection: keep-alive" --verbose'.format(ts.Variables.port),
    ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-200.gold"

# no rule for this
tr = Test.AddTestRun()
tr.MakeCurlCommand(
    ' --proxy 127.0.0.1:{0} "http://www.test.com/"  -H "Proxy-Connection: keep-alive" --verbose'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-404.gold"

# redirect result
tr = Test.AddTestRun()
tr.MakeCurlCommand(' --proxy 127.0.0.1:{0} "http://test3.com" --verbose'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-redirect.gold"

# referer hit
tr = Test.AddTestRun()
tr.MakeCurlCommand(
    ' --proxy 127.0.0.1:{0} "http://test4.com" --header "Referer: persia.com" --verbose'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-referer-hit.gold"

# referer miss
tr = Test.AddTestRun()
tr.MakeCurlCommand(
    ' --proxy 127.0.0.1:{0} "http://test4.com" --header "Referer: monkey.com" --verbose'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-referer-miss.gold"

# referer hit
tr = Test.AddTestRun()
tr.MakeCurlCommand(
    ' --proxy 127.0.0.1:{0} "http://test4.com" --header "Referer: www.persia.com" --verbose'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-referer-hit.gold"

# DNS test
tr = Test.AddTestRun()
tr.MakeCurlCommand(' --proxy 127.0.0.1:{0} "http://testDNS.com" --verbose'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-DNS-200.gold"

# microserver lookup test
tr = Test.AddTestRun()
tr.MakeCurlCommand(
    '--proxy 127.0.0.1:{0} "http://www.testexample.com/test" -H "Host: www.testexample.com" --verbose'.format(ts.Variables.port),
    ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server2)
tr.Processes.Default.Streams.stderr = "gold/lookupTest.gold"
tr.StillRunningAfter = server2
