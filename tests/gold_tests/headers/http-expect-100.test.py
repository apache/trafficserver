'''
Test handling of Expect: 100-continue header field.
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

import json
import os
import re
import subprocess

Test.Summary = '''
Test handling of Expect: 100-continue header field.
'''

Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=False)
server = Test.MakeOriginServer("server")

test_json_name = Test.Name + '.test.json'
test_json_desc = 'Test that 100 expected headers are replied to'
test_json = json.load(open(os.path.join(Test.TestDirectory, test_json_name), 'r'))

# Using the explicit index into 'steps' here, since there are only two steps for
# which we must configure the microserver.
req_data = test_json[test_json_desc]\
        ['transactions'][0]['steps'][-3]['TrafficServer -> Origin']
match = re.match('(.*?)(\r\n\r\n)(.*)', req_data, re.DOTALL)
request = {"headers": match.group(1)+match.group(2), "body": match.group(3), "timestamp": "1469733493.993"}
res_data = test_json[test_json_desc]\
        ['transactions'][0]['steps'][-2]['TrafficServer <- Origin']
response = {"headers": res_data, "timestamp": "1469733493.993"}
server.addResponse("sessionlog.json", request, response)

ts.Disk.records_config.update({
    'proxy.config.http.server_ports': 'ipv4:{0}'.format(ts.Variables.port),
    'proxy.config.http.send_100_continue_response': 1,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http*',
})

ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)

# Setup
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
Test.Setup.Copy(os.path.join(os.pardir, os.pardir, 'tools', 'json_runner.py'))
Test.Setup.Copy(test_json_name)

# Run
tr.Processes.Default.Command = "python json_runner.py '{}'".format(json.dumps(ts.Variables))
tr.Processes.Default.TimeOut = 5  # seconds

# Verify
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
expected_output = test_json[test_json_desc]\
        ['transactions'][0]['steps'][-1]['Client <- TrafficServer']
gold_file_path = os.path.join(Test.TestDirectory, Test.Name + '.gold')
with open(gold_file_path, 'w') as f:
    print('{}: PASS'.format(test_json_desc), file=f)
tr.Processes.Default.Streams.stdout = gold_file_path
