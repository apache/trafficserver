"""
Verify traffic_dump functionality.
"""
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
Test.Summary = '''
Verify traffic_dump functionality.
'''

Test.SkipUnless(
    Condition.PluginExists('traffic_dump.so'),
)

# Configure the origin server.
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 0\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

# Define ATS and configure
ts = Test.MakeATSProcess("ts")
replay_dir = os.path.join(ts.RunDirectory, "ts", "log")
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'traffic_dump',
})
ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)
# Configure traffic_dump.
ts.Disk.plugin_config.AddLine(
    'traffic_dump.so --logdir {0} --sample 1 --limit 1000000000'.format(replay_dir)
)

# Set up trafficserver expectations.
ts.Disk.diags_log.Content = Testers.ContainsExpression(
        "loading plugin.*traffic_dump.so",
        "Verify the traffic_dump plugin got loaded.")
ts.Streams.stderr = Testers.ContainsExpression(
        "Initialized with log directory: {0}".format(replay_dir),
        "Verify traffic_dump initialized with the configured directory.")
ts.Streams.stderr += Testers.ContainsExpression(
        "Initialized with sample pool size 1 bytes and disk limit 1000000000 bytes",
        "Verify traffic_dump initialized with the configured disk limit.")

# Set up the json replay file expectations.
replay_file_session_1 = os.path.join(replay_dir, "127", "0000000000000000")
ts.Disk.File(replay_file_session_1, exists=True)
replay_file_session_2 = os.path.join(replay_dir, "127", "0000000000000001")
ts.Disk.File(replay_file_session_2, exists=True)

# Execute the first transaction.
tr = Test.AddTestRun("First transaction")

tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = 'curl http://127.0.0.1:{0} -H\'Host: www.example.com\' --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Execute the second transaction.
tr = Test.AddTestRun("Second transaction")
tr.Processes.Default.Command = 'curl http://127.0.0.1:{0} -H\'Host: www.example.com\' --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Verify the properties of the replay file for the first transaction.
tr = Test.AddTestRun("Verify the json content of the first session")
verify_replay = "verify_replay.py"
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = "python3 {0} {1} {2}".format(
        verify_replay,
        os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
        replay_file_session_1)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Verify the properties of the replay file for the second transaction.
tr = Test.AddTestRun("Verify the json content of the second session")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = "python3 {0} {1} {2}".format(
        verify_replay,
        os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
        replay_file_session_2)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
