'''Verify a redirect whose header block ran out is handled on its framing alone.'''

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
Verify a redirect whose header block ran out is handled on its framing alone.
'''

Test.ContinueOnFail = True

# The companion malformed_redirect.test.py covers everything a replay origin can
# send. These cases cannot be written as a replay file: the origin has to close
# in the middle of the header block, and a replay origin always writes a
# complete message. Reading the socket directly also matters here, because the
# bug being guarded against is header text reaching the client as a body.
SERVER_SCRIPT = 'malformed_redirect_truncated_server.py'
CLIENT_SCRIPT = 'tcp_client.py'

server = Test.Processes.Process('origin')
Test.Setup.Copy(SERVER_SCRIPT)
Test.Setup.Copy(os.path.join(Test.Variables.AtsTestToolsDir, CLIENT_SCRIPT))
server_port = get_port(server, 'http_port')
server.Command = f'{sys.executable} {SERVER_SCRIPT} 127.0.0.1 {server_port}'
# No ReturnCode assertion. The origin runs until the test tears it down, and a
# torn down process is killed rather than asked to exit, so its exit status
# reports the signal instead of whether it did its job. What it did is asserted
# on its output below, and that it survived every run is asserted per run.
server.Ready = When.PortOpenv4(server_port)

ts = Test.MakeATSProcess('ts', enable_cache=False)
ts.Disk.records_config.update(
    {
        # The pooled case below asks whether a refused response left unread bytes on
        # a reusable origin connection. Session sharing defaults to a per thread
        # pool, so the follow up request would only consult the pool when it happens
        # to land on the same thread, and would otherwise open a fresh origin
        # connection and pass without ever testing anything.
        'proxy.config.http.server_session_sharing.pool': 'global',
    })
ts.Disk.remap_config.AddLine(f'map http://www.example.com http://127.0.0.1:{server_port}')

_request_files = {}


def request_file_for(path: str) -> str:
    """Write the request that asks the proxy for one path, once per path.

    :param path: The request target to ask the proxy for.
    :returns: The name of the file holding the request bytes.
    """
    if path not in _request_files:
        name = f'request{path.replace("/", "_")}.txt'
        Test.Disk.File(
            name, id=f'request_{len(_request_files)}').WriteOn(
                f'GET {path} HTTP/1.1\r\nHost: www.example.com\r\nConnection: close\r\n\r\n')
        _request_files[path] = name
    return _request_files[path]


def add_run(description: str, path: str, contains: list = None, excludes: list = None) -> None:
    """Fetch one path through the proxy and assert on the bytes that come back.

    An HTTP library repairs or rejects a response with contradictory framing
    before a test could look at it, and exits nonzero when it does, so these
    runs read the socket and print what arrived instead.

    :param description: What the run is checking, used as the run name.
    :param path: The request target to ask the proxy for.
    :param contains: Expressions the response must contain.
    :param excludes: Expressions the response must not contain.
    """
    tr = Test.AddTestRun(description)
    tr.TimeOut = 30
    tr.Processes.Default.Command = (f'{sys.executable} {CLIENT_SCRIPT} 127.0.0.1 {ts.Variables.port} {request_file_for(path)}')
    tr.Processes.Default.ReturnCode = 0
    for expression in contains or []:
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(expression, f'{description}: expected {expression}')
    for expression in excludes or []:
        tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(expression, f'{description}: unexpected {expression}')
    tr.StillRunningAfter = ts
    tr.StillRunningAfter = server


tr = Test.AddTestRun('Start the origin and the proxy.')
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = 'echo starting'
tr.Processes.Default.ReturnCode = 0

# Responses the proxy must still relay. An origin that closes without sending
# the blank line ending the header block still gets its redirect through,
# because the parser treats the close as the end of the block and nothing is
# left unread behind it. These are the cases a broken origin actually depends
# on, and they resolve as a completed parse, so they never needed a status code
# to be treated as a special case.
add_run(
    'A redirect whose header block merely ran out is still relayed.',
    '/truncated-302',
    contains=['HTTP/1.1 302 Found', 'Location: http://www.example.com/elsewhere', 'Content-Length: 0'])

add_run(
    'A relayed redirect keeps Transfer-Encoding and drops Content-Length.',
    '/truncated-te-cl-302',
    contains=['HTTP/1.1 302 Found', 'Transfer-Encoding: chunked'],
    excludes=['Content-Length:'])

add_run(
    'A relayed redirect carries one Content-Length, not the origin duplicate pair.',
    '/truncated-dup-cl-302',
    contains=['HTTP/1.1 302 Found', 'Content-Length: 5'])

# Running out of header does not make the framing trustworthy. This one ends the
# same way but its surviving fields disagree, so it must be refused.
add_run(
    'A redirect whose header block ran out with conflicting Content-Length values is refused.',
    '/truncated-conflicting-cl-302',
    contains=['HTTP/1.1 502'],
    excludes=['Location:', 'Content-Length: 11'])

# Refusing the response is only half the job. The origin connection still holds
# the part of the body the proxy never read, so reusing it would put those bytes
# at the head of the next response taken from that connection.
add_run(
    'A refused redirect leaves no unread origin bytes for a later response.',
    '/pooled-conflicting-cl-302',
    contains=['HTTP/1.1 502'],
    excludes=['Location:'])

add_run(
    'The next request is answered cleanly, not from a poisoned connection.',
    '/well-formed-302',
    contains=['HTTP/1.1 302 Found', 'Content-Length: 5'],
    excludes=['world'])

# Exactly one Content-Length must survive the duplicate pair, which a
# ContainsExpression on its own cannot show.
tr = Test.AddTestRun('The duplicate Content-Length pair is collapsed to a single field.')
tr.TimeOut = 30
tr.Processes.Default.Command = (
    f'{sys.executable} {CLIENT_SCRIPT} 127.0.0.1 {ts.Variables.port} {request_file_for("/truncated-dup-cl-302")} '
    '| grep -c "^Content-Length: 5"')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('^1$', 'Exactly one Content-Length field must survive.')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# The origin prints every request line it receives, so this pins that the paths
# were actually asked for rather than the origin having died.
server.Streams.All += Testers.ContainsExpression(
    'Received request line: GET /truncated-302', 'The origin must have been asked for the truncated redirect.')
server.Streams.All += Testers.ContainsExpression(
    'Received request line: GET /pooled-conflicting-cl-302', 'The origin must have been asked for the pooled redirect.')
