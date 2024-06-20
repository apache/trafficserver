'''
Test that redirects will be followed when refreshing stale cache objects.
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
Test that redirects will be followed when refreshing stale cache objects.
'''

server = Test.MakeOriginServer("server")

ArbitraryTimestamp = '12345678'

request_header = {
    'headers': ('GET /obj HTTP/1.1\r\n'
                'Host: *\r\n\r\n'),
    'timestamp': ArbitraryTimestamp,
    'body': ''
}
response_header = {
    'headers': ('HTTP/1.1 302 Found\r\n'
                'Location: http://127.0.0.1:{}/obj2\r\n\r\n'.format(server.Variables.Port)),
    'timestamp': ArbitraryTimestamp,
    'body': ''
}
server.addResponse('sessionfile.log', request_header, response_header)

request_header = {
    'headers': ('GET /obj2 HTTP/1.1\r\n'
                'Host: *\r\n\r\n'),
    'timestamp': ArbitraryTimestamp,
    'body': ''
}
response_header = {
    'headers': ('HTTP/1.1 200 OK\r\n'
                'X-Obj: obj2\r\n'
                'Cache-Control: max-age=2\r\n'
                'Content-Length: 0\r\n\r\n'),
    'timestamp': ArbitraryTimestamp,
    'body': ''
}
server.addResponse('sessionfile.log', request_header, response_header)

ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|dns|cache|redirect',
        'proxy.config.http.cache.required_headers': 0,  # Only Content-Length header required for caching.
        'proxy.config.http.push_method_enabled': 1,
        'proxy.config.url_remap.remap_required': 0,
        'proxy.config.http.redirect.actions': 'routable:follow,loopback:follow,self:follow',
        'proxy.config.http.number_of_redirections': 1
    })

# Set up to check the output after the tests have run.
#
log_id = Test.Disk.File("log2.txt")
log_id.Content = "gold/redirect_stale.gold"

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.Command = (
    r"printf 'GET /obj HTTP/1.1\r\nHost: 127.0.0.1:{}\r\n\r\n' | nc localhost {} >> log.txt".format(
        server.Variables.Port, ts.Variables.port))
tr.Processes.Default.ReturnCode = 0

# Wait for the response in cache to become stale, then GET it again.
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    r"sleep 4 ; printf 'GET /obj HTTP/1.1\r\nHost: 127.0.0.1:{}\r\n\r\n' | nc localhost {} >> log.txt".format(
        server.Variables.Port, ts.Variables.port))
tr.Processes.Default.ReturnCode = 0

# Filter out inconsistent content in test output.
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    r"grep -v -e '^Date: ' -e '^Age: ' -e '^Connection: ' -e '^Server: ATS/' log.txt | tr -d '\r'> log2.txt")
tr.Processes.Default.ReturnCode = 0
