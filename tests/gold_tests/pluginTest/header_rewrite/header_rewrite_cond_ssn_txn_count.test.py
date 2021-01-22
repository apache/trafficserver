'''
Test adding a close connection header when SSN-TXN-COUNT reach a limit
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

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

Test.testName = "SSN-TXN-COUNT"

# Test SSN-TXN-COUNT condition.
request_header_hello = {"headers":
                        "GET /hello HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 0\r\n\r\n",
                        "timestamp": "1469733493.993", "body": ""}
response_header_hello = {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\n"
                         "Content-Length: 0\r\n\r\n",
                         "timestamp": "1469733493.993", "body": ""}

request_header_world = {"headers": "GET /world HTTP/1.1\r\nContent-Length: 0\r\n"
                        "Host: www.example.com\r\n\r\n",
                        "timestamp": "1469733493.993", "body": "a\r\na\r\na\r\n\r\n"}
response_header_world = {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\n"
                         "Connection: close\r\nContent-Length: 0\r\n\r\n",
                         "timestamp": "1469733493.993", "body": ""}

# add request/response
server.addResponse("sessionlog.log", request_header_hello, response_header_hello)
server.addResponse("sessionlog.log", request_header_world, response_header_world)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'header.*',
    'proxy.config.http.auth_server_session_private': 1,
    'proxy.config.http.server_session_sharing.pool': 'global',
    'proxy.config.http.server_session_sharing.match': 'both',
})

# In case we need this in the future, just remove the comments.
# ts.Disk.logging_yaml.AddLines(
#     '''
# logging:
#   formats:
#     - name: long
#       format: "SSTC:%<sstc>"
#   logs:
#     - filename: rewrite.log.txt
#       format: long
# '''.split("\n")
# )

# This rule adds the connection header to close it after the number of transactions from a single
# session is > 2. This test the new SSN-TXN-COUNT condition.
ts.Setup.CopyAs('rules/rule_set_header_after_ssn_txn_count.conf', Test.RunDirectory)

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0} @plugin=header_rewrite.so @pparam={1}/rule_set_header_after_ssn_txn_count.conf'.format(
        server.Variables.Port, Test.RunDirectory))

curlRequest = (
    'curl -v -H\'Host: www.example.com\' -H\'Connection: keep-alive\' http://127.0.0.1:{0}/hello &&'
    'curl -v -H\'Host: www.example.com\' -H\'Connection: keep-alive\' http://127.0.0.1:{0}/hello &&'
    'curl -v -H\'Host: www.example.com\' -H\'Connection: keep-alive\' http://127.0.0.1:{0}/hello &&'
    'curl -v -H\'Host: www.example.com\' -H\'Connection: keep-alive\' http://127.0.0.1:{0}/hello &&'
    # I have to force last one with close connection header, this is also reflected in the response ^.
    # if I do not do this, then the microserver will fail to close and when shutting down the process will
    # fail with -9.
    'curl -v -H\'Host: www.example.com\' -H\'Connection: close\' http://127.0.0.1:{0}/world'
)

tr = Test.AddTestRun("Add connection close header when ssn-txn-count > 2")
tr.Processes.Default.Command = curlRequest.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = "gold/header_rewrite_cond_ssn_txn_count.gold"
tr.StillRunningAfter = server
