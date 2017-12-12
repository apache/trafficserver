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
Test basic redirection
'''

MAX_REDIRECT = 99

Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work")
)

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")
redirect_serv = Test.MakeOriginServer("re_server")
dest_serv = Test.MakeOriginServer("dest_server")
dns = Test.MakeDNServer("dns")

ts.Disk.records_config.update({
    'proxy.config.http.number_of_redirections': MAX_REDIRECT,
    'proxy.config.http.cache.http': 0,
    'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
    'proxy.config.dns.resolv_conf': 'NULL',
    'proxy.config.url_remap.remap_required': 0  # need this so the domain gets a chance to be evaluated through DNS
})

redirect_request_header = {"headers": "GET /redirect HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
redirect_response_header = {"headers": "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:{0}/redirectDest\r\n\r\n".format(
    dest_serv.Variables.Port), "timestamp": "5678", "body": ""}
dest_request_header = {"headers": "GET /redirectDest HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "11", "body": ""}
dest_response_header = {"headers": "HTTP/1.1 204 No Content\r\n\r\n", "timestamp": "22", "body": ""}

redirect_serv.addResponse("sessionfile.log", redirect_request_header, redirect_response_header)
dest_serv.addResponse("sessionfile.log", dest_request_header, dest_response_header)


dns.addRecords(records={"iwillredirect.com.": ["127.0.0.1"]})
# dns.addRecords(jsonFile="zone.json")

# if we don't disable remap_required, we can also just remap a domain to the domain recognized by DNS
# ts.Disk.remap_config.AddLine(
#     'map http://example.com http://iwillredirect.com:{1}/redirect'.format(redirect_serv.Variables.Port)
# )

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -i --proxy 127.0.0.1:{0} "http://iwillredirect.com:{1}/redirect"'.format(
    ts.Variables.port, redirect_serv.Variables.Port)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(redirect_serv)
tr.Processes.Default.StartBefore(dest_serv)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.Streams.stdout = "gold/redirect.gold"
tr.Processes.Default.ReturnCode = 0
