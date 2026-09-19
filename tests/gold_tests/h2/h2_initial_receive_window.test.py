"""Verify independent stream and connection receive windows for all policies."""
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

Test.Summary = "Verify preface and subsequent stream windows for flow-control policies 0, 1, and 2."
Test.ContinueOnFail = True

for policy in (0, 1, 2):
    for window in (65535, 32768):
        tr = Test.AddTestRun(f"Policy {policy} with stream window {window}")
        ts = tr.MakeATSProcess(f"ts-{policy}-{window}", enable_cache=False)
        ts.Disk.records_config.update(
            {
                'proxy.config.http.server_ports': f'{ts.Variables.port}:proto=http2',
                'proxy.config.http2.flow_control.policy_in': policy,
                'proxy.config.http2.initial_window_size_in': window,
                'proxy.config.http2.max_concurrent_streams_in': 100,
                'proxy.config.http.request_buffer_enabled': 1,
            })
        ts.Disk.remap_config.AddLine('map / http://127.0.0.1:1')
        tr.Setup.Copy('initial_window_client.py')
        tr.Processes.Default.StartBefore(ts)
        tr.Processes.Default.Command = (
            f"{sys.executable} {tr.RunDirectory}/initial_window_client.py {ts.Variables.port} {policy} {window}")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            f"verified policy={policy} stream_window={window}", "All advertised windows must match the policy.")
        tr.TimeOut = 15
