'''
Verify ATS handles HTTP/2 1xx interim responses from the origin.
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
import sys
from ports import get_port

Test.Summary = '''
Verify ATS correctly handles 1xx interim responses (e.g. 103 Early Hints) received
from an origin over HTTP/2, returning the final 200 to the client.
'''
Test.ContinueOnFail = True

ORIGIN = os.path.join(Test.TestDirectory, 'h2_interim_origin.py')

# Each mode is a distinct origin behavior, routed by request path.
#   single   : 103 then 200            (the deepwiki/Vercel case)
#   multi    : 103,103,100 then 200     (multiple sequential interims)
#   continue : 100 then 200
#   cont     : 103 split across HEADERS+CONTINUATION, then 200
#   none     : 200 only                 (control)
MODES = ['single', 'multi', 'continue', 'cont', 'none']
# 1xx with END_STREAM is malformed (RFC 9113 8.1); ATS must reject it so the client
# does not hang waiting for a final response that can never arrive.
INTERIM_ONLY = ['endstream']

ts = Test.MakeATSProcess("ts", enable_tls=True)
ts.addDefaultSSLFiles()
ts.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

# Create an origin process per mode and build the remap table from their ports.
origins = {}
remap_lines = []
for mode in MODES + INTERIM_ONLY:
    origin = Test.Processes.Process(f"origin-{mode}")
    port = get_port(origin, f"port_{mode}")
    origin.Command = f"{sys.executable} {ORIGIN} 127.0.0.1 {port} --mode {mode}"
    origin.Ready = When.PortOpenv4(port)
    origins[mode] = origin
    remap_lines.append(f"map http://ats.test/{mode} https://127.0.0.1:{port}/")

ts.Disk.remap_config.AddLines(remap_lines)
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
        'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.http.server_session_sharing.pool': 'thread',
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 4,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http2',
    })

first = True
for mode in MODES:
    tr = Test.AddTestRun(f"h2 origin interim response: mode={mode}")
    if first:
        for m in MODES + INTERIM_ONLY:
            tr.Processes.Default.StartBefore(origins[m])
        tr.Processes.Default.StartBefore(ts)
        first = False
    tr.MakeCurlCommand(f'-v -s -H "Host: ats.test" http://127.0.0.1:{ts.Variables.port}/{mode}', ts=ts)
    tr.Processes.Default.ReturnCode = 0
    tr.StillRunningAfter = ts
    for m in MODES + INTERIM_ONLY:
        tr.StillRunningAfter += origins[m]
    tr.Processes.Default.Streams.All += Testers.ContainsExpression(
        'HTTP/.* 200', f'mode={mode}: client must receive the final 200, not a 502')
    tr.Processes.Default.Streams.All += Testers.ContainsExpression(
        'interim-origin-body', f'mode={mode}: client must receive the 200 response body')

# 1xx + END_STREAM: ATS must reject the malformed interim response and fail the
# transaction promptly (a 5xx), not silently drop it and leave the client hanging.
tr = Test.AddTestRun("h2 origin interim response: mode=endstream (1xx with END_STREAM rejected)")
tr.MakeCurlCommand(f'-v -s -H "Host: ats.test" http://127.0.0.1:{ts.Variables.port}/endstream', ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression(
    'HTTP/.* 5[0-9][0-9]', 'endstream: client must get a 5xx error, not hang')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
    'interim-origin-body', 'endstream: client must not receive a 200 body')
