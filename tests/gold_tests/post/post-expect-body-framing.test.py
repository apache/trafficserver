'''
Verify that a POST body sent with Expect: 100-continue is never framed as a
second request when the origin answers before reading that body.
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

Test.Summary = '''
Verify a POST body carrying Expect: 100-continue is not framed as a new request
when the origin responds before reading it.
'''
Test.ContinueOnFail = True

# The POST body is itself a well formed request. If it survives unread on a
# keep-alive connection, the client sees two responses to its one request. The
# body arrives with the headers, after them, or chunked; only the first can ever
# be drained.
modes = {
    'immediate': False,
    'delayed': True,
    'chunked': True,
}

client = 'expect_client.py'
http_utils = 'http_utils.py'
# The Proxy Verifier server cannot drive this scenario: it only answers once the
# request body has been read, so it can never produce the early origin response
# that leaves the body unconsumed. mock_origin.py replies as soon as the headers
# arrive, which is exactly what is needed here.
mock_origin = os.path.join(Test.Variables.AtsTestToolsDir, 'mock_origin.py')

ts = Test.MakeATSProcess("ts", enable_cache=False)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'http',
})

servers = {}
for mode in modes:
    Test.GetTcpPort(f'{mode}_port')
    port = Test.Variables[f'{mode}_port']
    ts.Disk.remap_config.AddLine(f'map /{mode} http://127.0.0.1:{port}')
    server = Test.Processes.Process(f'server-{mode}', f'{sys.executable} {mock_origin} {port}')
    server.Ready = When.PortOpen(port)
    servers[mode] = server

for mode, expects_connection_close in modes.items():
    tr = Test.AddTestRun(f'Expect: 100-continue POST with {mode} body.')
    tr.Setup.Copy(client)
    tr.Setup.Copy(http_utils)
    tr.Processes.Default.StartBefore(servers[mode])
    if mode == 'immediate':
        tr.Processes.Default.StartBefore(ts)
    tr.Processes.Default.Command = (
        f'{sys.executable} {client} 127.0.0.1 {ts.Variables.port} --mode {mode} --path /{mode} -s example.com')
    tr.Processes.Default.ReturnCode = 0
    tr.StillRunningAfter = ts

    # The property under test: one request in, exactly one response out.
    tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
        'RESPONSES_RECEIVED=1', 'The body must not be answered as a second request.')
    tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
        'X-Embedded', 'The embedded request must not be reflected back to the client.')

    if expects_connection_close:
        # The body was not available to drain, so keep-alive must be dropped
        # rather than leaving unread bytes to frame the next request. This is the
        # same contract post-early-return.test.py asserts for the non-Expect case.
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'CONNECTION_CLOSE_HEADER=yes', 'An undrainable body must close the connection.')
