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

Test.Summary = 'Test that Trafficserver starts with default configurations.'
Test.SkipUnless(Condition.HasProgram('curl', 'Curl need to be installed on system for this test to work'))
Test.SkipUnless(Condition.HasProgram('gdb', 'GDB need to be installed on system for this test to work'))
Test.ContinueOnFail = True

ts = Test.MakeATSProcess('ts')
server = Test.MakeOriginServer('server')

Test.testName = ''
request_header = {
    'headers': 'GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n', 
    'timestamp': '1469733493.993', 
    'body': ''}
response_header = {
    'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
    'timestamp': '1469733493.993', 
    'body': ''
}
server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 1,
    'proxy.config.accept_threads': 10
})

ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --proxy http://127.0.0.1:{0} http://www.example.com -H "Proxy-Connection: Keep-Alive" --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.Streams.stderr = 'gold/http_200.gold'

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'python3 {0} {1}'.format(Test.TestDirectory + '/start_gdb.py', Test.TestDirectory + '/gdb_cmd.txt')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = 'gold/thread_1_10.gold'
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression('\[ET\_NET 1\]', 'there should be more than 1 threads')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression('\[ACCEPT 10\:', 'there should be more than 10 threads')
