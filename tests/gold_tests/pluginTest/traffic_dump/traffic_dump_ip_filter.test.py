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
import sys

Test.Summary = '''
Verify traffic_dump IP filter functionality.
'''

Test.SkipUnless(
    Condition.PluginExists('traffic_dump.so'),
)

# Configure the origin server.
replay_file = "replay/traffic_dump.yaml"
server = Test.MakeVerifierServerProcess("server", replay_file)


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
        f'map / http://127.0.0.1:{server.Variables.http_port}'
    )
    # Configure traffic_dump as specified.
    ts.Disk.plugin_config.AddLine(plugin_command.format(replay_dir))

    ts_dump_fil = os.path.join(replay_dir, "127", "0000000000000000")
    ts.Disk.File(ts_dump_fil, exists=replay_exists)
    return ts, ts_dump_fil


# Common verification variables.
verify_replay = "verify_replay.py"
schema = os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json')
verify_command_prefix = f'{sys.executable} {verify_replay} {schema}'

#
# Test 1: Verify -4 works for a specified address.
#
tr = Test.AddTestRun("Verify that -4 matches 127.0.0.1 as expected")
ts1, ts1_replay_file = get_common_ats_process(
    "ts1",
    'traffic_dump.so --logdir {0} --sample 1 --limit 1000000000 -4 127.0.0.1',
    replay_exists=True)
ts1.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Filtering to only dump connections with ip: 127.0.0.1",
    "Verify the IP filter status message.")
tr.AddVerifierClientProcess(
    "client0", replay_file, http_ports=[ts1.Variables.port],
    other_args='--keys 1')

tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts1)
tr.StillRunningAfter = server
tr.StillRunningAfter = ts1

# Verify that the expected request body was recorded.
tr = Test.AddTestRun("Verify that the expected request body was recorded.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = f'{verify_command_prefix} {ts1_replay_file}'
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
ts2.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Filtering to only dump connections with ip: 1.2.3.4",
    "Verify the IP filter status message.")
tr.AddVerifierClientProcess(
    "client1", replay_file, http_ports=[ts2.Variables.port],
    other_args='--keys 1')

tr.Processes.Default.StartBefore(ts2)
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
    f"Problems parsing IP filter address argument: {invalid_ip}",
    "Verify traffic_dump detects an invalid IPv4 address.")
tr.AddVerifierClientProcess(
    "client2", replay_file, http_ports=[ts3.Variables.port],
    other_args='--keys 1')

tr.Processes.Default.StartBefore(ts3)
tr.StillRunningAfter = server
tr.StillRunningAfter = ts3
