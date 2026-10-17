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
"""
Test that an exception escaping a hook on an ET_NET thread aborts traffic_server
through its terminate handler.

The crash_test plugin throws a std::runtime_error from its READ_REQUEST_HDR hook
when it receives a specific header. This test then verifies that:
1. traffic_server reports the exception type and message before aborting
2. A crash log file was created
3. The crash log backtrace for the crashing thread includes the throwing frame
"""

import os
import re

Test.Summary = '''
Test that an uncaught exception on an ET_NET thread is reported before traffic_server aborts.
'''

# Create an origin server for the test.
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "Hello"}
server.addResponse("sessionlog.json", request_header, response_header)

ts = Test.MakeATSProcess("ts")

# The terminate handler aborts, so ATS exits on SIGABRT.
ts.ReturnCode = -6

ts.Disk.records_config.update(
    {
        'proxy.config.proxy_name': 'test_proxy',
        'proxy.config.url_remap.remap_required': 0,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'crash_test',
        # Enable the crash log helper.
        'proxy.config.crash_log_helper': 'traffic_crashlog',
    })

# Copy the crash_test plugin.
plugin_path = os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'pluginTest', 'crash_test', '.libs', 'crash_test.so')
ts.Setup.Copy(plugin_path, ts.Env['PROXY_CONFIG_PLUGIN_PLUGIN_DIR'])

ts.Disk.plugin_config.AddLine("crash_test.so")

ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{server.Variables.Port}/")

ts.Disk.diags_log.Content += Testers.ContainsExpression(
    "Received throw trigger header - throwing now!", "Expect the log indicating the intentional throw.")
ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    "This should never be reached.", "Expect to not see the log after the throw.")

# The terminate handler writes to stderr, which lands in traffic.out. The
# demangled type name is matched loosely in case the standard library puts it
# in an inline namespace.
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"traffic_server: terminating on an unhandled std::\S*runtime_error: crash_test throw",
    "Expect the terminate handler to report the exception type and message.")

# Test 1: Make a normal request to verify the server is running.
tr = Test.AddTestRun("Verify server is running")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.MakeCurlCommand(f'-s -o /dev/null -w "%{{http_code}}" http://127.0.0.1:{ts.Variables.port}/', ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("200", "Expected 200 OK response")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 2: Send the throw trigger header.
tr = Test.AddTestRun("Trigger throw")
# The curl command should fail since ATS will abort.
tr.MakeCurlCommand(f'-s -o /dev/null -H "X-Crash-Test: throw" http://127.0.0.1:{ts.Variables.port}/', ts=ts)
tr.Processes.Default.ReturnCode = 52

# Test 3: Wait for a crash log to be created.
tr = Test.AddTestRun("Wait for crash log")
crash_log_glob = f'{ts.Variables.LOGDIR}/crash-*.log'
# Wait up to 60 seconds for a crash log file to appear, then 1 extra second for it to be written.
tr.Processes.Default.Command = (f"{os.path.join(Test.Variables.AtsTestToolsDir, 'condwait')} 60 1 -f '{crash_log_glob}'")
tr.Processes.Default.ReturnCode = 0

# Test 4: Verify crash log contains expected content.
tr = Test.AddTestRun("Check crash log content")
tr.Processes.Default.Command = (f'cat {ts.Variables.LOGDIR}/crash-*.log 2>&1')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("Aborted", "Expected crash log to show the abort signal")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("Crashing Thread", "Expected crashing thread backtrace first")
# The terminate handler runs before the stack unwinds, so the frame that threw
# is still on the crashing thread's stack. re.MULTILINE makes the tester match
# across lines, so the frame must fall between the crashing thread header and
# the blank line that ends that thread's backtrace.
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    r"Crashing Thread[^\n]*:\n(?:[^\n]+\n)*?[^\n]*crash_test_throw\(\)",
    "Expected the throwing frame in the crashing thread backtrace",
    reflags=re.MULTILINE)
