'''
Verify an origin connection is not reused when a request body was left unsent.

When the origin answers a POST before ATS has finished forwarding the body, ATS
still owes that origin the remainder of the body. The connection must not go back
into the session pool: the next request written to it would be consumed as the
tail of that body instead of being served.
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
Verify an origin connection is not reused when a request body was left unsent.
'''
Test.ContinueOnFail = True

Test.GetTcpPort('origin_port')

ts = Test.MakeATSProcess('ts', select_ports=True, enable_cache=False)
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

tr = Test.AddTestRun('POST answered early, then a following request')
tr.Processes.Default.Command = '{0} {1}/early_response_client.py {2}'.format(sys.executable, Test.RunDirectory, ts.Variables.port)
tr.Processes.Default.StartBefore(origin)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# The early response must still reach the client, unchanged from before.
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    r'EARLY-POST STATUS HTTP/1\.1 420 Be Calm', 'The early response should be returned to the client.')

# The following request must be served. If the origin connection were reused it
# would be consumed as the tail of the previous body and never answered.
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    r'PROBE STATUS HTTP/1\.1 200 OK', 'The following request should be served, not swallowed.')

# Seen from the origin: the second request must arrive as a request, and nothing
# request-shaped may turn up inside the discarded body.
origin.Streams.stdout += Testers.ContainsExpression(r'REQUEST GET /probe', 'The origin should receive the following request.')
origin.Streams.stdout += Testers.ExcludesExpression(
    r'CONSUMED-AS-BODY', 'No request may be consumed as the body of the earlier POST.')
