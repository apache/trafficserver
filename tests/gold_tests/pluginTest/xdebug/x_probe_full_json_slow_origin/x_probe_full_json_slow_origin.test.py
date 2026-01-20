'''
Verify xdebug plugin probe-full-json does not hang with slow origin servers.

This test reproduces a bug where the transform would enter an infinite loop
when the origin server delays sending the response body. The bug occurs because
the transform would reenable and call back upstream even when no data was
available to consume.
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
Test xdebug probe-full-json with slow origin to verify no infinite loop
'''

Test.SkipUnless(Condition.PluginExists('xdebug.so'))
Test.SkipUnless(Condition.HasProgram("nc", "nc (netcat) is required for custom slow server"))

# Configure ATS
ts = Test.MakeATSProcess("ts", enable_cache=False)

ts.Disk.records_config.update(
    {
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "xdebug_transform",
        # Set reasonable timeouts
        "proxy.config.http.transaction_no_activity_timeout_in": 10,
        "proxy.config.http.transaction_no_activity_timeout_out": 10,
    })

ts.Disk.plugin_config.AddLine('xdebug.so --enable=probe-full-json')

# Reserve a port for the custom slow server
Test.GetTcpPort("server_port")

ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{Test.Variables.server_port}/")

# Start the custom slow-body server
server = Test.Processes.Process(
    "server",
    f"bash -c '{Test.TestDirectory}/slow-body-server.sh {Test.Variables.server_port} {Test.RunDirectory}/server_request.txt'")

# Test with probe-full-json=nobody (which triggers the bug most easily)
tr = Test.AddTestRun("Verify probe-full-json with slow body delivery")
tr.TimeOut = 15  # Should complete well under this; timeout indicates hang/loop

# Make the request - use timeout to detect if the request hangs
tr.Processes.Default.Command = (
    f'timeout 8 curl -s -o /dev/null -w "%{{http_code}}" '
    f'-H "Host: example.com" '
    f'-H "X-Debug: probe-full-json=nobody" '
    f'http://127.0.0.1:{ts.Variables.port}/test')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)

# Should get 200, not timeout (which would cause non-zero return and 124 output)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("200", "Should receive 200 OK, not timeout")

# Verify no infinite loop by checking the logs
# The bug manifests as the transform being called with "bytes of body is expected"
# but no data consumed. Every "expected" should be followed by "consumed".
tr2 = Test.AddTestRun("Verify no infinite loop in transform")
tr2.Processes.Default.Command = f"bash {Test.TestDirectory}/verify_no_loop.sh {ts.Variables.LOGDIR}/traffic.out"
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "PASS", "Verification script should pass - every 'expected' followed by 'consumed'")
