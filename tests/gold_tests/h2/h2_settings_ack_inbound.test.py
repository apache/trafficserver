"""Verify client ACKs for dynamic stream windows do not exhaust the limit."""
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

Test.Summary = "Verify inbound SETTINGS ACK accounting with concurrent streams."

ts = Test.MakeATSProcess("ts", enable_tls=True)
ts.addDefaultSSLFiles()
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
        'proxy.config.http2.flow_control.policy_in': 2,
        'proxy.config.http2.max_settings_frames_per_minute': 2,
        'proxy.config.http.request_buffer_enabled': 1,
        'proxy.config.http2.initial_window_size_in': 65535,
        'proxy.config.http2.max_concurrent_streams_in': 100,
    })
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:1')
ts.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

tr = Test.AddTestRun("Acknowledge dynamic windows with three open streams")
tr.Setup.Copy("settings_ack_client.py")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = f"{sys.executable} {tr.RunDirectory}/settings_ack_client.py {ts.Variables.ssl_port}"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "dynamic_settings_acknowledged=[3-9]", "At least three solicited SETTINGS ACKs must be accepted.")
tr.StillRunningAfter = ts
tr.TimeOut = 15

tr = Test.AddTestRun("Verify inbound ACKs did not trigger connection errors")
tr.Processes.Default.Command = (
    f"{Test.Variables.AtsTestToolsDir}/stdout_wait 10"
    f" 'traffic_ctl metric get proxy.process.http2.max_settings_frames_per_minute_exceeded"
    f" proxy.process.http2.connection_errors'"
    f" {Test.TestDirectory}/gold/h2-settings-ack-metrics.gold")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
