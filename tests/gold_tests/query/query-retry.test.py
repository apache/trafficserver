'''
Verify QUERY is not retried once its request, content included, has been sent
to an origin that then closes the connection, whether or not request buffering
is enabled.
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

import re
import shlex
import sys

from ports import get_port

Test.Summary = '''
Verify QUERY is not retried once its request, content included, has been sent
to an origin that then closes the connection, whether or not request buffering
is enabled.
'''

Test.ContinueOnFail = True

BODY = 'SELECT * FROM t WHERE a'


def add_scenario(name: str, method: str, buffering: bool) -> None:
    tr = Test.AddTestRun(f'{method} after the origin closes, request buffering {"enabled" if buffering else "disabled"}')
    tr.Setup.Copy('query_retry_origin.py')

    server = Test.Processes.Process(f'origin-{name}')
    server_port = get_port(server, 'http_port')
    server.Command = f'{sys.executable} {tr.RunDirectory}/query_retry_origin.py {server_port}'
    server.Ready = When.PortOpen(server_port)

    ts = Test.MakeATSProcess(f'ts-{name}')
    ts.Disk.records_config.update(
        {
            'proxy.config.http.request_buffer_enabled': 1 if buffering else 0,
            'proxy.config.http.connect_attempts_max_retries': 3,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http_trans',
        })
    ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server_port}/')

    content = f"-H 'Content-Type: application/sql' --data-binary {shlex.quote(BODY)} " if method == 'QUERY' else ''
    tr.Processes.Default.StartBefore(server)
    tr.Processes.Default.StartBefore(ts)
    tr.Processes.Default.Command = (
        f"curl -s --max-time 30 -o /dev/null -w '%{{http_code}}\\n' -X {method} {content}http://127.0.0.1:{ts.Variables.port}/search"
    )
    tr.Processes.Default.ReturnCode = 0

    if method == 'QUERY':
        server.Streams.stdout += Testers.ContainsExpression(
            f'request attempt=1 method=QUERY body={re.escape(BODY)}', 'The origin must receive the QUERY and its content.')
        server.Streams.stdout += Testers.ExcludesExpression(
            'request attempt=2 method=', 'ATS must not retry a QUERY after sending it.')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('^502$', 'The failed QUERY must not be retried.')
    else:
        server.Streams.stdout += Testers.ContainsExpression(
            f'request attempt=2 method={method}', f'ATS must retry a {method} after the origin closes.')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('^200$', f'The retried {method} must succeed.')


# GET shows that the origin's close does lead ATS to retry a safe method.
add_scenario('get', 'GET', buffering=False)
add_scenario('query-buffered', 'QUERY', buffering=True)
add_scenario('query-unbuffered', 'QUERY', buffering=False)
