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
Test basic post redirection
'''

# TODO figure out how to use this
MAX_REDIRECT = 99

Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work"),
    Condition.HasProgram("truncate", "truncate need to be installed on system for this test to work")
)

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")
redirect_serv1 = Test.MakeOriginServer("re_server1")
redirect_serv2 = Test.MakeOriginServer("re_server2")
dest_serv = Test.MakeOriginServer("dest_server")

ts.Disk.records_config.update({
    'proxy.config.http.number_of_redirections': MAX_REDIRECT,
    'proxy.config.http.post_copy_size' : 919430601,
    'proxy.config.http.cache.http': 0,
    'proxy.config.http.redirect.actions': 'self:follow', # redirects to self are not followed by default
    # 'proxy.config.diags.debug.enabled': 1,
})

redirect_request_header = {"headers": "POST /redirect1 HTTP/1.1\r\nHost: *\r\nContent-Length: 52428800\r\n\r\n", "timestamp": "5678", "body": ""}
redirect_response_header = {"headers": "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:{0}/redirect2\r\n\r\n".format(
    redirect_serv2.Variables.Port), "timestamp": "5678", "body": ""}

redirect_request_header2 = {"headers": "POST /redirect2 HTTP/1.1\r\nHost: *\r\nContent-Length: 52428800\r\n\r\n", "timestamp": "5678", "body": ""}
redirect_response_header2 = {"headers": "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:{0}/redirectDest\r\n\r\n".format(
    dest_serv.Variables.Port), "timestamp": "5678", "body": ""}

dest_request_header = {"headers": "POST /redirectDest HTTP/1.1\r\nHost: *\r\nContent-Length: 52428800\r\n\r\n", "timestamp": "11", "body": ""}
dest_response_header = {"headers": "HTTP/1.1 204 No Content\r\n\r\n", "timestamp": "22", "body": ""}

redirect_serv1.addResponse("sessionfile.log", redirect_request_header, redirect_response_header)
redirect_serv2.addResponse("sessionfile.log", redirect_request_header2, redirect_response_header2)
dest_serv.addResponse("sessionfile.log", dest_request_header, dest_response_header)

ts.Disk.remap_config.AddLine(
    'map http://127.0.0.1:{0} http://127.0.0.1:{1}'.format(ts.Variables.port, redirect_serv1.Variables.Port)
)

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'touch largefile.txt && truncate largefile.txt -s 50M && curl -i http://127.0.0.1:{0}/redirect1 -F "filename=@./largefile.txt" && rm -f largefile.txt'.format(ts.Variables.port)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(redirect_serv1)
tr.Processes.Default.StartBefore(redirect_serv2)
tr.Processes.Default.StartBefore(dest_serv)
tr.Processes.Default.Streams.stdout = "gold/redirect_post.gold"
tr.Processes.Default.ReturnCode = 0
