'''
Test the ESI plugin's HTTP fetch request size cap.
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
Verify HttpDataFetcherImpl rejects ESI include fetches whose total HTTP
request size (request line + URL + forwarded headers) would exceed
MAX_REQ_LEN (32 KB), logging an error rather than allocating a buffer
sized by an overflowed size_t.
'''

Test.SkipUnless(Condition.PluginExists('esi.so'),)

# Matches MAX_REQ_LEN in plugins/esi/fetcher/HttpDataFetcherImpl.cc.
MAX_REQ_LEN = 32 * 1024

# total_len in addFetchRequest is:
#     sizeof("GET ") - 1               ->  4
#   + url.length()
#   + sizeof(" HTTP/1.0\r\n") - 1      -> 11
#   + _headers_str.length()
#   + sizeof("\r\n") - 1               ->  2
# A path one byte longer than MAX_REQ_LEN guarantees the cap is tripped
# regardless of how many headers end up being forwarded.
oversized_path = 'A' * (MAX_REQ_LEN + 1)
esi_body = ('<html>\n<body>\n'
            f'Hello, <esi:include src="http://www.example.com/{oversized_path}"/>\n'
            '</body>\n</html>\n')

server = Test.MakeOriginServer("server")
server.addResponse(
    "sessionfile.log", {
        "headers": ("GET /oversized.php HTTP/1.1\r\n"
                    "Host: www.example.com\r\n"
                    "Content-Length: 0\r\n\r\n"),
        "timestamp": "1469733493.993",
        "body": ""
    }, {
        "headers":
            (
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "X-Esi: 1\r\n"
                "Connection: close\r\n"
                f"Content-Length: {len(esi_body)}\r\n"
                "Cache-Control: max-age=300\r\n\r\n"),
        "timestamp": "1469733493.993",
        "body": esi_body
    })

ts = Test.MakeATSProcess("ts")
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|plugin_esi',
})
ts.Disk.remap_config.AddLine(f'map http://www.example.com/ http://127.0.0.1:{server.Variables.Port}')
ts.Disk.plugin_config.AddLine('esi.so')

tr = Test.AddTestRun("Start the server and ATS.")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = "echo starting"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Issue a request whose ESI include URL exceeds the 32 KB fetch cap.")
tr.MakeCurlCommand(
    f'http://127.0.0.1:{ts.Variables.port}/oversized.php '
    '-H"Host: www.example.com" -H"Accept: */*" --output /dev/null --silent',
    ts=ts)
tr.Processes.Default.ReturnCode = 0
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    r"HTTP request size exceeds maximum 32768", "ESI fetcher must log the MAX_REQ_LEN cap error for oversize requests")
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
