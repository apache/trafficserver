'''
Verify that a plugin's VCONN_CLOSE hook runs when the connection closes while it is
parked in a TLS handshake hook.
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
A connection that closes while parked in a TLS handshake hook must still deliver
TS_VCONN_CLOSE_HOOK to the plugin.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion("1.1.1"),)

ts = Test.MakeATSProcess("ts", enable_tls=True)
server = Test.MakeOriginServer("server")
server.addResponse(
    "sessionlog.json", {
        "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, {
        "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    })

ts.addDefaultSSLFiles()

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.show_location': 0,
        'proxy.config.diags.debug.tags': 'ssl_hook_test',
        # Fire the handshake timeout while the plugin still has the handshake parked (2s park).
        'proxy.config.ssl.handshake_timeout_in': 1,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    })

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts.Disk.remap_config.AddLine(
    'map https://example.com:{1} http://127.0.0.1:{0}'.format(server.Variables.Port, ts.Variables.ssl_port))

# The delayed client hello callback parks the handshake for 2 seconds before it reenables.
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_hook_test.so'), ts, '-client_hello=1 -close=1')

# Give up after 1 second, which is inside the 2 second park, so the connection closes while it
# is still suspended in the client hello hook. curl reports operation timed out (exit 28).
tr = Test.AddTestRun("Client disconnects while parked in the client hello hook")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.MakeCurlCommand('-k --max-time 1 -H \'host:example.com:{0}\' https://127.0.0.1:{0}'.format(ts.Variables.ssl_port), ts=ts)
tr.Processes.Default.ReturnCode = 28
tr.Processes.Default.TimeOut = 15
tr.TimeOut = 15

# The handshake really was parked.
ts.Disk.traffic_out.Content = Testers.ContainsExpression("Client Hello callback 0", "the handshake parked in the client hello hook")

# The close hook must still fire, with the correct event. Before the fix, callHooks() advanced
# curHook within the client hello hook list instead of the close hook list, so this never ran.
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Close callback 0 .* - event is good", "the close hook ran for the parked connection")
