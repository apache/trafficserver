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
Test CPPAPI example plugin DelayTransformation
'''

Test.SkipUnless(
    Condition.PluginExists("DelayTransformationPlugin.so")
)

server = Test.MakeOriginServer("server")

resp_body = "1234567890" "1234567890" "1234567890" "1234567890" "1234567890" "\n"

server.addResponse(
    "sessionlog.json",
    {
        "headers":
            "GET / HTTP/1.1\r\n"
            "Host: does_not_matter"
            "\r\n",
        "timestamp": "1469733493.993",
    },
    {
        "headers":
            "HTTP/1.1 200 OK\r\n"
            "Connection: close\r\n"
            f"Content-Length: {len(resp_body)}\r\n"
            "\r\n",
        "timestamp": "1469733493.993",
        "body": resp_body
    }
)

ts = Test.MakeATSProcess("ts")

ts.Disk.plugin_config.AddLine("DelayTransformationPlugin.so")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'delay_transformation',
})

ts.Disk.remap_config.AddLine(
    f'map http://xyz/ http://127.0.0.1:{server.Variables.Port}/'
)

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = f"curl --verbose --proxy 127.0.0.1:{ts.Variables.port} http://xyz/"
tr.Processes.Default.Streams.stdout = "body.gold"
tr.Processes.Default.ReturnCode = 0
