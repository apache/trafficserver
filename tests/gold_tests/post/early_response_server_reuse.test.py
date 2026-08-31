'''
Verify an origin connection is not reused when a request body tunnel is abandoned.

When the origin answers before ATS has finished forwarding a request body, ATS
still owes that origin the remainder of the body. The connection must not go back
into the session pool: the next request written to it would be consumed as the
tail of that body instead of being served.

Two release paths are covered, because they are decided in different places:

  abandoned-tunnel  POST answered 420 early. Builds a response tunnel, so the
                    close is decided in tunnel_handler_server.
  revalidate-304    body-carrying GET on a stale entry answered 304 early. Builds
                    no response tunnel, so that check cannot run and
                    release_server_session() decides instead.
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

import sys

Test.Summary = '''
Verify an origin connection is not reused when a request body tunnel is abandoned.
'''
Test.ContinueOnFail = True

Test.GetTcpPort('origin_port')

# The cache is on so the 304 revalidation path can be reached at all.
ts = Test.MakeATSProcess('ts', select_ports=True, enable_cache=True)
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}/'.format(Test.Variables.origin_port))
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        # One event thread so both transactions use the same session pool, making
        # reuse deterministic rather than dependent on which thread is picked.
        'proxy.config.exec_thread.limit': 1,
        'proxy.config.exec_thread.autoconfig': 0,
    })

Test.Setup.CopyAs('early_response_origin.py', Test.RunDirectory)
Test.Setup.CopyAs('early_response_client.py', Test.RunDirectory)

origin = Test.Processes.Process(
    'origin', '{0} {1}/early_response_origin.py {2}'.format(sys.executable, Test.RunDirectory, Test.Variables.origin_port))
origin.Ready = When.PortOpen(Test.Variables.origin_port)

# Seen from the origin, across both scenarios: nothing request-shaped may ever
# turn up inside a discarded body.
origin.Streams.stdout += Testers.ExcludesExpression(
    r'CONSUMED-AS-BODY', 'No request may be consumed as the body of an earlier request.')
origin.Streams.stdout += Testers.ContainsExpression(r'REQUEST GET /probe', 'The origin should receive the following request.')

first = True
for scenario, early_status, early_label in [
    ('abandoned-tunnel', r'HTTP/1\.1 420 Be Calm', 'EARLY-POST'),
    ('revalidate-304', r'HTTP/1\.1 200 OK', 'REVALIDATE'),
]:
    tr = Test.AddTestRun('{0}: early response, then a following request'.format(scenario))
    tr.Processes.Default.Command = '{0} {1}/early_response_client.py {2} {3}'.format(
        sys.executable, Test.RunDirectory, ts.Variables.port, scenario)
    if first:
        tr.Processes.Default.StartBefore(origin)
        tr.Processes.Default.StartBefore(ts)
        first = False
    tr.Processes.Default.ReturnCode = 0
    tr.StillRunningAfter = ts
    tr.StillRunningAfter += origin

    # The early response must still reach the client, unchanged from before. For
    # the revalidation this is the cached copy, served because the origin said 304.
    tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
        r'{0} STATUS {1}'.format(early_label, early_status), 'The early response should be returned to the client.')

    # The following request must be served. If the origin connection were reused
    # it would be consumed as the tail of the previous body and never answered.
    tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
        r'PROBE STATUS HTTP/1\.1 200 OK', 'The following request should be served, not swallowed.')
