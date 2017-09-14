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

import os
import subprocess

Test.Summary = '''
'''

Test.SkipUnless(Condition.HasProgram("netstat", "netstat needs to be installed on system for this test to work"))
Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=False)
server = Test.MakeOriginServer("server")

arbitrary_content = 'c4e1a38a-b79a-45d1-a0c6-1fe8b1b05380'
# We only need one transaction as only the VIA header will be checked.
request_header = {"headers": "POST / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": arbitrary_content}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.http.server_ports': 'ipv4:{0}'.format(ts.Variables.port),
    'proxy.config.http.send_100_continue_response': '1',
})

ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)

# Ask the OS if the port is ready for connect()
def CheckPort(Port):
    return lambda: 0 == subprocess.call('netstat --listen --tcp -n | grep -q :{}'.format(Port), shell=True)

tr = Test.AddTestRun()
# Wait for the micro server
tr.Processes.Default.StartBefore(server, ready=CheckPort(server.Variables.Port))
# Delay on readiness of our ssl ports
tr.Processes.Default.StartBefore(Test.Processes.ts)

Test.Setup.Copy('json_runner.py')
Test.Setup.Copy(os.path.join(os.pardir, os.pardir, 'tools', 'tcp_client.py'))
test_json_name = Test.Name + '.test.json'
Test.Setup.Copy(test_json_name)
import json
tr.Processes.Default.Command = "python json_runner.py '{}'".format(json.dumps(ts.Variables))

#tr.Processes.Default.Command = 'curl --verbose --ipv4 --http1.1 --proxy localhost:{} http://www.example.com'.format(
#    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

tr.StillRunningAfter = server
tr.StillRunningAfter = ts
test_json = json.load(open(os.path.join(Test.TestDirectory, test_json_name), 'r'))
expected_output = test_json['Test that 100 expected headers are replied to']\
        ['transactions'][0]['steps'][-1]['Client <- TrafficServer']
gold_file_path = os.path.join(Test.TestDirectory, Test.Name + '.gold')
with open(gold_file_path, 'w') as f:
    f.write(expected_output)
tr.Processes.Default.Streams.stdout = gold_file_path
