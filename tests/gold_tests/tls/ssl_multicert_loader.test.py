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
Test reloading ssl_multicert.config with errors and keeping around the old ssl config structure
'''

sni_domain = 'example.com'

ts = Test.MakeATSProcess("ts", command="traffic_manager", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")
server2 = Test.MakeOriginServer("server3")
request_header = {"headers": f"GET / HTTP/1.1\r\nHost: {sni_domain}\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
    'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
})

ts.addDefaultSSLFiles()

ts.Disk.remap_config.AddLine(
    f'map / http://127.0.0.1:{server.Variables.Port}'
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

tr = Test.AddTestRun("ensure we can connect for SNI $sni_domain")
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(server)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = f"curl -q -s -v -k --resolve '{sni_domain}:{ts.Variables.ssl_port}:127.0.0.1' https://{sni_domain}:{ts.Variables.ssl_port}"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
tr.Processes.Default.Streams.stderr = Testers.IncludesExpression(f"CN={sni_domain}", "Check response")


tr2 = Test.AddTestRun("Update config files")
# Update the configs
sslcertpath = ts.Disk.ssl_multicert_config.AbsPath

tr2.Disk.File(sslcertpath, id="ssl_multicert_config", typename="ats:config")
tr2.Disk.ssl_multicert_config.AddLines([
    'ssl_cert_name=server_does_not_exist.pem ssl_key_name=server_does_not_exist.key',
    'dest_ip=* ssl_cert_name=server.pem_doesnotexist ssl_key_name=server.key',
])
tr2.StillRunningAfter = ts
tr2.StillRunningAfter = server
tr2.Processes.Default.Command = 'echo Updated configs'
tr2.Processes.Default.Env = ts.Env
tr2.Processes.Default.ReturnCode = 0

tr2reload = Test.AddTestRun("Reload config")
tr2reload.StillRunningAfter = ts
tr2reload.StillRunningAfter = server
tr2reload.Processes.Default.Command = 'traffic_ctl config reload'
tr2reload.Processes.Default.Env = ts.Env
tr2reload.Processes.Default.ReturnCode = 0
ts.Disk.diags_log.Content = Testers.ContainsExpression('ERROR: ', 'ERROR')

# Reload of ssl_multicert.config should fail, BUT the old config structure
# should be in place to successfully answer for the test domain
tr3 = Test.AddTestRun("Make request again for $sni_domain")
# Wait for the reload to complete
tr3.Processes.Default.StartBefore(server2, ready=When.FileContains(ts.Disk.diags_log.Name, 'failed to load certificate ', 1))
tr3.StillRunningAfter = ts
tr3.StillRunningAfter = server
tr3.Processes.Default.Command = f"curl -q -s -v -k --resolve '{sni_domain}:{ts.Variables.ssl_port}:127.0.0.1' https://{sni_domain}:{ts.Variables.ssl_port}"
tr3.Processes.Default.ReturnCode = 0
tr3.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
tr3.Processes.Default.Streams.stderr = Testers.IncludesExpression(f"CN={sni_domain}", "Check response")


##########################################################################
# Ensure ATS fails/exits when non-existent cert is specified
# Also, not explicitly setting proxy.config.ssl.server.multicert.exit_on_load_fail
# to catch if the current default (1) changes in the future

ts2 = Test.MakeATSProcess("ts2", command="traffic_manager", select_ports=True, enable_tls=True)
ts2.Disk.ssl_multicert_config.AddLines([
    'dest_ip=* ssl_cert_name=server.pem_doesnotexist ssl_key_name=server.key',
])

tr4 = Test.AddTestRun()
tr4.Processes.Default.Command = 'echo Waiting'
tr4.Processes.Default.ReturnCode = 0
tr4.Processes.Default.StartBefore(ts2)

ts2.ReturnCode = 2
ts2.Ready = 0  # Need this to be 0 because we are testing shutdown, this is to make autest not think ats went away for a bad reason.
ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
    'Traffic Server is fully initialized',
    'process should fail when invalid certificate specified')
ts2.Disk.diags_log.Content = Testers.IncludesExpression('FATAL: failed to load SSL certificate file', 'check diags.log"')
