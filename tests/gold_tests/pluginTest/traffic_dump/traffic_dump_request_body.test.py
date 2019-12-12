"""
Verify traffic_dump request body functionality.
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
Verify traffic_dump request body functionality.
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
request_header = {"headers": "POST /with_content_length HTTP/1.1\r\n"
                  "Host: www.example.com\r\n"
                  "Content-Length: 4\r\n\r\n",
                  "timestamp": "1469733493.993", "body": "1234"}
response_header = {"headers": "HTTP/1.1 200 OK\r\n"
                   "Connection: close\r\n"
                   "Content-Length: 4\r\n\r\n",
                   "timestamp": "1469733493.993", "body": "1234"}
server.addResponse("sessionfile.log", request_header, response_header)


def get_common_ats_process(name):
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
    # Configure traffic_dump to dump body bytes (-b).
    ts.Disk.plugin_config.AddLine(
        'traffic_dump.so --logdir {0} --sample 1 --limit 1000000000 -b'.format(replay_dir)
    )

    ts_replay_file_session_1 = os.path.join(replay_dir, "127", "0000000000000000")
    ts.Disk.File(ts_replay_file_session_1, exists=True)

    ts.Streams.stderr = Testers.ContainsExpression(
        "Dumping body bytes: true",
        "Verify that dumping body bytes is enabled.")
    return ts, ts_replay_file_session_1


#
# Test 1: Verify a request without a body is dumped correctly.
#
tr = Test.AddTestRun("An empty request body is handled correctly.")
ts1, ts1_replay_file = get_common_ats_process("ts1")

tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts1)
tr.Processes.Default.Command = 'curl http://127.0.0.1:{0}/empty -H"Host: www.notls.com" --verbose'.format(
    ts1.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200_get.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts1

tr = Test.AddTestRun("Verify the json content of the first session")
verify_replay = "verify_replay.py"
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = 'python3 {0} {1} {2}'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    ts1_replay_file)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts1

#
# Test 2: Verify request body can be dumped.
#
tr = Test.AddTestRun("Verify body bytes can be dumped")
ts2, ts2_replay_file = get_common_ats_process("ts2")
ts2.Streams.stderr += Testers.ContainsExpression(
    "Got a request body of size 4 bytes",
    "Verify logging of the dumped body bytes.")

request_body = "1234"

tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts2)
tr.Processes.Default.Command = \
    ('curl http://127.0.0.1:{0}/with_content_length -H"Host: www.example.com" '
     '--verbose -d "{1}"'.format(
         ts2.Variables.port,
         request_body))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200_post.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts2

# Verify that the expected request body was recorded.
tr = Test.AddTestRun("Verify that the expected request body was recorded.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = "python3 {0} {1} {2} --request_body {3}".format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    ts2_replay_file,
    request_body)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts2

#
# Test 3: Verify request body bytes are escaped.
#
tr = Test.AddTestRun("Verify body bytes are dumped")
ts3, ts3_replay_file = get_common_ats_process("ts3")
ts3.Streams.stderr += Testers.ContainsExpression(
    "Got a request body of size 5 bytes",
    "Verify logging of the dumped body bytes.")

request_body = '12"34'

tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts3)
tr.Processes.Default.Command = \
    ("curl http://127.0.0.1:{0}/with_content_length -H'Host: www.example.com' "
     "--verbose -d '{1}'".format(
         ts3.Variables.port,
         request_body))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200_post.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts3

# Verify that the expected request body was recorded.
tr = Test.AddTestRun("Verify that the expected request body was recorded.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = "python3 {0} {1} {2} --request_body '{3}'".format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    ts3_replay_file,
    r'12"34')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts3
