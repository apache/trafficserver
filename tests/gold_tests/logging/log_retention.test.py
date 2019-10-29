'''
Verify correct log retention behavior.
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
Test the enforcment of proxy.config.log.max_space_mb_for_logs.
'''

# Create and configure the ATS process.
ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update({
    # Do not accept connections from clients until cache subsystem is operational.
    'proxy.config.http.wait_for_cache': 1,

    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'logspace',

    'proxy.config.log.rolling_enabled': 3,
    'proxy.config.log.auto_delete_rolled_files': 1,

    # 10 MB is the minimum rolling size.
    'proxy.config.log.rolling_size_mb': 10,
    'proxy.config.log.periodic_tasks_interval': 1,
    # The following configures a 12 MB log cap with a required 2 MB head room.
    # Thus the rotated log of just over 10 MB should be deleted because it
    # will not leave enough head room.
    'proxy.config.log.max_space_mb_headroom': 2,
    'proxy.config.log.max_space_mb_for_logs': 12,
})


# Configure approximately 5 KB entries.
ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: long
      format: "{prefix}: %<sssc>"
  logs:
    - filename: test_rotation
      format: long
'''.format(prefix="0123456789"*500).split("\n")
)

# Verify from traffic.out that the rotated log file was auto-deleted.
ts.Streams.stderr = Testers.ContainsExpression(
        "logical space used.*space is not available",
        "It was detected that space was not available")
ts.Streams.stderr += Testers.ContainsExpression(
        "auto-deleting.*test_rotation.log",
        "Verify the test log file got deleted")
ts.Streams.stderr += Testers.ContainsExpression(
        "The rolled logfile.*was auto-deleted.*bytes were reclaimed",
        "Verify that space was reclaimed")

# Create and configure microserver.
server = Test.MakeOriginServer("server")
request_header = {"headers": "GET / HTTP/1.1\r\nHost: does.not.matter\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=85000\r\n\r\n",
                   "timestamp": "1469733493.993", "body": "xxx"}
server.addResponse("sessionlog.json", request_header, response_header)
ts.Disk.remap_config.AddLine(
    'map http://127.0.0.1:{0} http://127.0.0.1:{1}'.format(ts.Variables.port, server.Variables.Port)
)

# The first test run starts the required processes.
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl "http://127.1.1.1:{0}" --verbose ; '.format(
        ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
server.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(Test.Processes.server)

tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# With the following test run, we instigate a log rotation via entries from a
# few thousand curl requests.
tr = Test.AddTestRun()
# At 5K a log entry, we need a lot of curl'd requests to get to the 10 MB roll
# minimum.
curl_commands = 'for i in {{1..2500}}; do curl "http://127.1.1.1:{0}" --verbose; done'.format(
    ts.Variables.port)
tr.Processes.Default.Command = curl_commands
tr.Processes.Default.ReturnCode = 0

tr.StillRunningAfter = ts
tr.StillRunningAfter = server
