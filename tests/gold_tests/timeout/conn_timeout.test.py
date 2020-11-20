'''
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

Test.Summary = 'Testing ATS TCP handshake timeout'

# Skipping this in the normal CI because it requires privilege.
# Comment out to run in your privileged environment
Test.SkipIf(Condition.true("Test requires privilege"))

ts = Test.MakeATSProcess("ts")

Test.ContinueOnFail = True
Test.GetTcpPort("blocked_upstream_port")
Test.GetTcpPort("upstream_port")

server4 = Test.MakeOriginServer("server4")

ts.Disk.records_config.update({
    'proxy.config.url_remap.remap_required': 1,
    'proxy.config.http.connect_attempts_timeout': 2,
    'proxy.config.http.post_connect_attempts_timeout': 2,
    'proxy.config.http.connect_attempts_max_retries': 0,
    'proxy.config.http.transaction_no_activity_timeout_out': 5,
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'http',
})

ts.Disk.remap_config.AddLine('map /blocked http://10.1.1.1:{0}'.format(Test.Variables.blocked_upstream_port))
ts.Disk.remap_config.AddLine('map /not-blocked http://10.1.1.1:{0}'.format(Test.Variables.upstream_port))

ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: testformat
      format: '%<pssc> %<cquc> %<pscert> %<cscert>'
  logs:
    - mode: ascii
      format: testformat
      filename: squid
'''.split("\n")
)


# Set up the network name space.  Requires privilege
tr = Test.AddTestRun("tr-ns-setup")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.TimeOut = 2
tr.Setup.Copy('setupnetns.sh')
tr.Processes.Default.Command = 'echo start; sudo sh -x ./setupnetns.sh {0} {1}'.format(
    Test.Variables.blocked_upstream_port, Test.Variables.upstream_port)

# Request to the port that is blocked in the network ns.  The SYN should never be responded to
# and the connect timeout should trigger with a 50x return.  If the SYN handshake occurs, the
# no activity timeout would trigger, but not before the test timeout expires
tr = Test.AddTestRun("tr-blocking")
tr.Processes.Default.Command = 'curl -i http://127.0.0.1:{0}/blocked {0}'.format(ts.Variables.port)
tr.Processes.Default.TimeOut = 4
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "HTTP/1.1 502 internal error - server connection terminated", "Connect failed")

tr = Test.AddTestRun("tr-blocking-post")
tr.Processes.Default.Command = 'curl -d "stuff" -i http://127.0.0.1:{0}/blocked {0}'.format(ts.Variables.port)
tr.Processes.Default.TimeOut = 4
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "HTTP/1.1 502 internal error - server connection terminated", "Connect failed")


#  Should not catch the connect timeout.  Even though the first bytes are not sent until after the 2 second connect timeout
#  But before the no-activity timeout
tr = Test.AddTestRun("tr-delayed")
tr.Setup.Copy('delay-server.sh')
tr.Setup.Copy('case1.sh')
tr.Processes.Default.Command = 'sh ./case1.sh {0} {1}'.format(ts.Variables.port, ts.Variables.upstream_port)
tr.Processes.Default.TimeOut = 7
tr.Processes.Default.Streams.All = Testers.ContainsExpression("HTTP/1.1 200", "Connect succeeded")


# cleanup the network namespace and virtual network
tr = Test.AddTestRun("tr-cleanup")
tr.Processes.Default.Command = 'sudo ip netns del testserver; sudo ip link del veth0 type veth peer name veth1'
tr.Processes.Default.TimeOut = 4

tr = Test.AddTestRun("Wait for the access log to write out")
tr.Processes.Default.StartBefore(server4, ready=When.FileExists(ts.Disk.squid_log))
tr.StillRunningAfter = ts
tr.Processes.Default.Command = 'echo "log file exists"'
tr.Processes.Default.ReturnCode = 0
