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

request_header = {"headers": "GET /one HTTP/1.1\r\n"
                  "Host: www.example.com\r\nContent-Length: 0\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK"
                   "\r\nConnection: close\r\nContent-Length: 0"
                   "\r\nSet-Cookie: classified_not_for_logging\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)
request_header = {"headers": "GET /two HTTP/1.1\r\n"
                  "Host: www.example.com\r\nContent-Length: 0\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK"
                   "\r\nConnection: close\r\nContent-Length: 0"
                   "\r\nSet-Cookie: classified_not_for_logging\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)
request_header = {"headers": "GET /three HTTP/1.1\r\n"
                  "Host: www.example.com\r\nContent-Length: 0\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK"
                   "\r\nConnection: close\r\nContent-Length: 0"
                   "\r\nSet-Cookie: classified_not_for_logging\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)
request_header = {"headers": "GET /post_with_body HTTP/1.1\r\n"
                  "Host: www.example.com\r\nContent-Length: 0\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK"
                   "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)
request_header = {"headers": "GET /cache_test HTTP/1.1\r\n"
                  "Host: www.example.com\r\nContent-Length: 0\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK"
                   "\r\nConnection: close\r\nCache-Control: max-age=300\r\n"
                   "Content-Length: 4\r\n\r\n",
                   "timestamp": "1469733493.993", "body": "1234"}
server.addResponse("sessionfile.log", request_header, response_header)
request_header = {"headers": "GET /first HTTP/1.1\r\n"
                  "Host: www.example.com\r\nContent-Length: 0\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK"
                   "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)
request_header = {"headers": "GET /second HTTP/1.1\r\n"
                  "Host: www.example.com\r\nContent-Length: 0\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK"
                   "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

# Define ATS and configure it.
ts = Test.MakeATSProcess("ts")
replay_dir = os.path.join(ts.RunDirectory, "ts", "log")
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'traffic_dump',
    'proxy.config.http.insert_age_in_response': 0,
})
ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)
# Configure traffic_dump.
ts.Disk.plugin_config.AddLine(
    'traffic_dump.so --logdir {0} --sample 1 --limit 1000000000 '
    '--sensitive-fields "cookie,set-cookie,x-request-1,x-request-2"'.format(replay_dir)
)
# Configure logging of transactions. This is helpful for the cache test below.
ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: basic
      format: "%<cluc>: Read result: %<crc>:%<crsc>:%<chm>, Write result: %<cwr>"
  logs:
    - filename: transactions
      format: basic
'''.split('\n'))

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
ts.Streams.stderr += Testers.ContainsExpression(
    "Finish a session with log file of.*bytes",
    "Verify traffic_dump sees the end of sessions and accounts for it.")

# Set up the json replay file expectations.
replay_file_session_1 = os.path.join(replay_dir, "127", "0000000000000000")
ts.Disk.File(replay_file_session_1, exists=True)
replay_file_session_2 = os.path.join(replay_dir, "127", "0000000000000001")
ts.Disk.File(replay_file_session_2, exists=True)
replay_file_session_3 = os.path.join(replay_dir, "127", "0000000000000002")
ts.Disk.File(replay_file_session_3, exists=True)
replay_file_session_4 = os.path.join(replay_dir, "127", "0000000000000003")
ts.Disk.File(replay_file_session_4, exists=True)
replay_file_session_5 = os.path.join(replay_dir, "127", "0000000000000004")
ts.Disk.File(replay_file_session_5, exists=True)
replay_file_session_6 = os.path.join(replay_dir, "127", "0000000000000005")
ts.Disk.File(replay_file_session_6, exists=True)
replay_file_session_7 = os.path.join(replay_dir, "127", "0000000000000006")
ts.Disk.File(replay_file_session_7, exists=True)

#
# Test 1: Verify the correct behavior of two transactions across two sessions.
#

# Execute the first transaction.
tr = Test.AddTestRun("First transaction")

tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = \
    ('curl --http1.1 http://127.0.0.1:{0}/one -H"Cookie: donotlogthis" '
     '-H"Host: www.example.com" -H"X-Request-1: ultra_sensitive" --verbose'.format(
         ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
http_protocols = "tcp,ipv4"

# Execute the second transaction.
tr = Test.AddTestRun("Second transaction")
tr.Processes.Default.Command = \
    ('curl http://127.0.0.1:{0}/two -H"Host: www.example.com" '
     '-H"X-Request-2: also_very_sensitive" --verbose'.format(
         ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Verify the properties of the replay file for the first transaction.
tr = Test.AddTestRun("Verify the json content of the first session")
verify_replay = "verify_replay.py"
sensitive_fields_arg = (
    "--sensitive-fields cookie "
    "--sensitive-fields set-cookie "
    "--sensitive-fields x-request-1 "
    "--sensitive-fields x-request-2 ")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = 'python3 {0} {1} {2} {3} --client-protocols "{4}"'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_1,
    sensitive_fields_arg,
    http_protocols)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Verify the properties of the replay file for the second transaction.
tr = Test.AddTestRun("Verify the json content of the second session")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = "python3 {0} {1} {2} {3} --request-target '/two'".format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_2,
    sensitive_fields_arg)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 2: Verify the correct behavior of an explicit path in the request line.
#

# Verify that an explicit path in the request line is recorded.
tr = Test.AddTestRun("Make a request with an explicit target.")
request_target = "http://localhost:{0}/candy".format(ts.Variables.port)
tr.Processes.Default.Command = (
    'curl --request-target "{0}" '
    'http://127.0.0.1:{1}/three -H"Host: www.example.com" --verbose'.format(
        request_target, ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/explicit_target.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify the replay file has the explicit target.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)

tr.Processes.Default.Command = "python3 {0} {1} {2} {3} --request-target '{4}'".format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_3,
    sensitive_fields_arg,
    request_target)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 3: Verify correct handling of a POST with body data.
#

tr = Test.AddTestRun("Make a POST request with a body.")
request_target = "http://localhost:{0}/post_with_body".format(ts.Variables.port)

# Send the replay file as the request body because it is conveniently already
# in the test run directory.
tr.Processes.Default.Command = (
    'curl --data-binary @{0} --request-target "{1}" '
    'http://127.0.0.1:{2} -H"Host: www.example.com" --verbose'.format(
        verify_replay, request_target, ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/post_with_body.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify the client-request size node has the expected value.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)

size_of_verify_replay_file = os.path.getsize(os.path.join(Test.TestDirectory, verify_replay))
tr.Processes.Default.Command = \
    "python3 {0} {1} {2} {3} --client-request-size {4}".format(
        verify_replay,
        os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
        replay_file_session_4,
        sensitive_fields_arg,
        size_of_verify_replay_file)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 4: Verify correct handling of a response produced out of the cache.
#
tr = Test.AddTestRun("Make a request for an uncached object.")
tr.Processes.Default.Command = \
    ('curl --http1.1 http://127.0.0.1:{0}/cache_test -H"Host: www.example.com" --verbose'.format(
        ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/4_byte_response_body.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Repeat the previous request: should be cached now.")
tr.Processes.Default.Command = \
    ('curl --http1.1 http://127.0.0.1:{0}/cache_test -H"Host: www.example.com" --verbose'.format(
        ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/4_byte_response_body.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify that the cached response's replay file looks appropriate.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = 'python3 {0} {1} {2} --client-protocols "{3}"'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_6,
    http_protocols)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 5: Verify correct handling of two transactions in a session.
#
tr = Test.AddTestRun("Conduct two transactions in the same session.")
tr.Processes.Default.Command = \
    ('curl --http1.1 http://127.0.0.1:{0}/first -H"Host: www.example.com" --verbose --next '
     'curl --http1.1 http://127.0.0.1:{0}/second -H"Host: www.example.com" --verbose'
            .format(ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/two_transactions.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify that the dump file can be read.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = 'python3 {0} {1} {2} --client-protocols "{3}"'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_7,
    http_protocols)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
