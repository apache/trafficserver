"""
Verify traffic_dump IP filter functionality.
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
Verify traffic_dump IP filter functionality.
'''

Test.SkipUnless(
    Condition.PluginExists('traffic_dump.so'),
)

# Configure the origin server.
server = Test.MakeOriginServer("server", both=True)

request_header = {"headers": "GET /empty HTTP/1.1\r\n"
                  "Host: www.notls.com\r\n"
                  "Content-Length: 0\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\n"
                   "Connection: close\r\n"
                   "Content-Length: 0\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)


def get_common_ats_process(name, plugin_command, replay_exists):
    """
    Get an ATS process with some common configuration.

    These tests have different log expectations, but have generally the same
    ATS Process configuration. This function returns a Process with that common
    configuration.

    Returns:
        A configured ATS Process.
        The replay file.
    """
    ts = Test.MakeATSProcess(name)
    replay_dir = os.path.join(ts.RunDirectory, name, "log")
    ts.Disk.records_config.update({
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'traffic_dump',
    })
    ts.Disk.remap_config.AddLine(
        'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
    )
    # Configure traffic_dump as specified.
    ts.Disk.plugin_config.AddLine(plugin_command.format(replay_dir))

    ts_replay_file_session_1 = os.path.join(replay_dir, "127", "0000000000000000")
    ts.Disk.File(ts_replay_file_session_1, exists=replay_exists)
    return ts, ts_replay_file_session_1


#
# Test 1: Verify -4 works for a specified address.
#
tr = Test.AddTestRun("Verify that -4 matches 127.0.0.1 as expected")
ts1, ts1_replay_file = get_common_ats_process(
    "ts1",
    'traffic_dump.so --logdir {0} --sample 1 --limit 1000000000 -4 127.0.0.1',
    replay_exists=True)
ts1.Streams.stderr += Testers.ContainsExpression(
    "Filtering to only dump connections with ip: 127.0.0.1",
    "Verify the IP filter status message.")

tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts1)
tr.Processes.Default.Command = \
    ('curl http://127.0.0.1:{0}/empty -H"Host: www.notls.com" --verbose'.format(
        ts1.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200_get.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts1

# Verify that the expected request body was recorded.
tr = Test.AddTestRun("Verify that the expected request body was recorded.")
verify_replay = "verify_replay.py"
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = "python3 {0} {1} {2}".format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    ts1_replay_file)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts1

#
# Test 2: Verify -4 filters out other addresses.
#
tr = Test.AddTestRun("Verify that -4 filters out our non-matching IP as expected")
ts2, ts2_replay_file = get_common_ats_process(
    "ts2",
    'traffic_dump.so --logdir {0} --sample 1 --limit 1000000000 -4 1.2.3.4',
    replay_exists=False)
ts2.Streams.stderr += Testers.ContainsExpression(
    "Filtering to only dump connections with ip: 1.2.3.4",
    "Verify the IP filter status message.")

tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts2)
tr.Processes.Default.Command = \
    ('curl http://127.0.0.1:{0}/empty -H"Host: www.notls.com" --verbose'.format(
        ts2.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200_get.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts2

#
# Test 3: Verify -4 recognizes an invalid IP address string.
#
tr = Test.AddTestRun("Verify that -4 detects an invalid IP string")
invalid_ip = "this_is_not_a_valid_ip_string"
ts3, ts3_replay_file = get_common_ats_process(
    "ts3",
    'traffic_dump.so --logdir {0} --sample 1 --limit 1000000000 -4 ' + invalid_ip,
    replay_exists=False)
ts3.Disk.diags_log.Content = Testers.ContainsExpression(
    "Problems parsing IP filter address argument: {}".format(invalid_ip),
    "Verify traffic_dump detects an invalid IPv4 address.")

tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts3)
tr.Processes.Default.Command = \
    ('curl http://127.0.0.1:{0}/empty -H"Host: www.notls.com" --verbose'.format(
        ts3.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200_get.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts3
