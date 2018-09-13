'''
Test redirection
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
Test redirection
'''

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")
redirect_serv = Test.MakeOriginServer("re_server")
dest_serv = Test.MakeOriginServer("dest_server")
dns = Test.MakeDNServer("dns")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|dns|redirect',
    'proxy.config.http.number_of_redirections': 1,
    'proxy.config.http.cache.http': 0,
    'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
    'proxy.config.dns.resolv_conf': 'NULL',
    'proxy.config.url_remap.remap_required': 0,  # need this so the domain gets a chance to be evaluated through DNS
    'proxy.config.http.redirect.actions': 'self:follow', # redirects to self are not followed by default
})

Test.Setup.Copy(os.path.join(Test.Variables.AtsTestToolsDir,'tcp_client.py'))

redirect_request_header = {"headers": "GET /redirect HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
redirect_response_header = {"headers": "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:{0}/redirectDest\r\n\r\n".format(
    dest_serv.Variables.Port), "timestamp": "5678", "body": ""}
redirect_serv.addResponse("sessionfile.log", redirect_request_header, redirect_response_header)

dest_request_header = {"headers": "GET /redirectDest HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "11", "body": ""}
dest_response_header = {"headers": "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n", "timestamp": "22", "body": ""}
dest_serv.addResponse("sessionfile.log", dest_request_header, dest_response_header)

dns.addRecords(records={"iwillredirect.test": ["127.0.0.1"]})

data_dirname = 'generated_test_data'
data_path = os.path.join(Test.TestDirectory, data_dirname)
os.makedirs(data_path, exist_ok=True)

# Here and below: spaces are deliberately omitted from the test run names because autest creates directories using these names.
tr = Test.AddTestRun("FollowsRedirectWithAbsoluteLocationURI")
# Here and below: because autest's Copy does not behave like standard cp, it's easiest to write all of our files out and copy last.
with open(os.path.join(data_path, tr.Name), 'w') as f:
    f.write('GET /redirect HTTP/1.1\r\nHost: iwillredirect.test:{port}\r\n\r\n'.format(port=redirect_serv.Variables.Port))
tr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | egrep -v '^(Date: |Server: ATS/)'".format(ts.Variables.port, os.path.join(data_dirname, tr.Name))
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(redirect_serv)
tr.Processes.Default.StartBefore(dest_serv)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.Streams.stdout = "gold/redirect.gold"
tr.Processes.Default.ReturnCode = 0



redirect_request_header = {"headers": "GET /redirect-relative-path HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
redirect_response_header = {"headers": "HTTP/1.1 302 Found\r\nLocation: /redirect\r\n\r\n", "timestamp": "5678", "body": ""}
redirect_serv.addResponse("sessionfile.log", redirect_request_header, redirect_response_header)

redirect_request_header = {"headers": "GET /redirect HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
redirect_response_header = {"headers": "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n", "timestamp": "22", "body": ""}
redirect_serv.addResponse("sessionfile.log", redirect_request_header, redirect_response_header)

tr = Test.AddTestRun("FollowsRedirectWithRelativeLocationURI")
with open(os.path.join(data_path, tr.Name), 'w') as f:
    f.write('GET /redirect-relative-path HTTP/1.1\r\nHost: iwillredirect.test:{port}\r\n\r\n'.format(port=redirect_serv.Variables.Port))
tr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | egrep -v '^(Date: |Server: ATS/)'".format(ts.Variables.port, os.path.join(data_dirname, tr.Name))
tr.StillRunningAfter = ts
tr.StillRunningAfter = redirect_serv
tr.StillRunningAfter = dest_serv
tr.StillRunningAfter = dns
tr.Processes.Default.Streams.stdout = "gold/redirect.gold"
tr.Processes.Default.ReturnCode = 0



redirect_request_header = {"headers": "GET /redirect-relative-path-no-leading-slash HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
redirect_response_header = {"headers": "HTTP/1.1 302 Found\r\nLocation: redirect\r\n\r\n", "timestamp": "5678", "body": ""}
redirect_serv.addResponse("sessionfile.log", redirect_request_header, redirect_response_header)

tr = Test.AddTestRun("FollowsRedirectWithRelativeLocationURIMissingLeadingSlash")
with open(os.path.join(data_path, tr.Name), 'w') as f:
    f.write('GET /redirect-relative-path-no-leading-slash HTTP/1.1\r\nHost: iwillredirect.test:{port}\r\n\r\n'.format(port=redirect_serv.Variables.Port))
tr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | egrep -v '^(Date: |Server: ATS/)'".format(ts.Variables.port, os.path.join(data_dirname, tr.Name))
tr.StillRunningAfter = ts
tr.StillRunningAfter = redirect_serv
tr.StillRunningAfter = dest_serv
tr.StillRunningAfter = dns
tr.Processes.Default.Streams.stdout = "gold/redirect.gold"
tr.Processes.Default.ReturnCode = 0


for status,phrase in sorted({
        301:'Moved Permanently',
        302:'Found',
        303:'See Other',
        305:'Use Proxy',
        307:'Temporary Redirect',
        308:'Permanent Redirect',
        }.items()):

    redirect_request_header = {
            "headers": ("GET /redirect{0} HTTP/1.1\r\n"
                        "Host: *\r\n\r\n").\
                                format(status),
            "timestamp": "5678",
            "body": ""}
    redirect_response_header = {
            "headers": ("HTTP/1.1 {0} {1}\r\n"
                        "Connection: close\r\n"
                        "Location: /redirect\r\n\r\n").\
                                format(status, phrase),
            "timestamp": "5678",
            "body": ""}
    redirect_serv.addResponse("sessionfile.log", redirect_request_header, redirect_response_header)

    tr = Test.AddTestRun("FollowsRedirect{0}".format(status))
    with open(os.path.join(data_path, tr.Name), 'w') as f:
        f.write(('GET /redirect{0} HTTP/1.1\r\n'
                'Host: iwillredirect.test:{1}\r\n\r\n').\
                        format(status, redirect_serv.Variables.Port))
    tr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | egrep -v '^(Date: |Server: ATS/)'".\
            format(ts.Variables.port, os.path.join(data_dirname, tr.Name))
    tr.StillRunningAfter = ts
    tr.StillRunningAfter = redirect_serv
    tr.StillRunningAfter = dest_serv
    tr.StillRunningAfter = dns
    tr.Processes.Default.Streams.stdout = "gold/redirect.gold"
    tr.Processes.Default.ReturnCode = 0

Test.Setup.Copy(data_path)
