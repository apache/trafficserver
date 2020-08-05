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
Test.Summary = '''

'''
Test.SkipUnless(Condition.PluginExists('cookie_remap.so'))
Test.ContinueOnFail = True
Test.testName = "cookie_remap: set HTTP status of the response"

# Define default ATS
ts = Test.MakeATSProcess("ts")

# Setup the remap configuration
config_path = os.path.join(Test.TestDirectory, "configs/statusconfig.txt")
with open(config_path, 'r') as config_file:
    config1 = config_file.read()

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'cookie_remap.*|http.*|dns.*',
})

ts.Disk.File(ts.Variables.CONFIGDIR + "/statusconfig.txt", exists=False, id="config1")
ts.Disk.config1.WriteOn(config1)

ts.Disk.remap_config.AddLine(
    'map http://www.example.com/magic http://shouldnothit.com @plugin=cookie_remap.so @pparam=config/statusconfig.txt'
)

# Plugin sets the HTTP status because first rule matches
tr = Test.AddTestRun("Sets the status to 205")
tr.Processes.Default.Command = '''
curl \
--proxy 127.0.0.1:{0} \
"http://www.example.com/magic" \
-H"Cookie: fpbeta=magic" \
-H "Proxy-Connection: keep-alive" \
--verbose \
'''.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts

tr.Streams.All = "gold/matchstatus.gold"

# Plugin sets the HTTP status because the else rule matches (i.e. no match)
tr = Test.AddTestRun("Sets the else status to 400")
tr.Processes.Default.Command = '''
curl \
--proxy 127.0.0.1:{0} \
"http://www.example.com/magic" \
-H "Proxy-Connection: keep-alive" \
-H "Proxy-Connection: keep-alive" \
--verbose \
'''.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

tr.Streams.All = "gold/matchelsestatus.gold"
