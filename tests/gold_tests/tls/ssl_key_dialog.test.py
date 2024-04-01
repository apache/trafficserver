'''
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding right ownership.  The ASF licenses this file
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
Test that Trafficserver starts with default configurations.
'''

ts = Test.MakeATSProcess("ts", enable_tls=True)
server = Test.MakeOriginServer("server")

ts.addSSLfile("ssl/passphrase.pem")
ts.addSSLfile("ssl/passphrase.key")

ts.addSSLfile("ssl/passphrase2.pem")
ts.addSSLfile("ssl/passphrase2.key")

ts.Disk.remap_config.AddLine(f"map https://passphrase:{ts.Variables.ssl_port}/ http://127.0.0.1:{server.Variables.Port}")
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ssl_load|http',
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    })

ts.Disk.ssl_multicert_config.AddLines(
    [
        'dest_ip=* ssl_cert_name=passphrase.pem ssl_key_name=passphrase.key ssl_key_dialog="exec:/bin/bash -c \'echo -n passphrase\'"',
    ])

request_header = {"headers": "GET / HTTP/1.1\r\nHost: bogus\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "success!"}
server.addResponse("sessionlog.json", request_header, response_header)

tr = Test.AddTestRun("use a key with passphrase")
tr.Setup.Copy("ssl/signer.pem")
tr.Processes.Default.Command = f"curl -v --cacert ./signer.pem  --resolve 'passphrase:{ts.Variables.ssl_port}:127.0.0.1' https://passphrase:{ts.Variables.ssl_port}/"
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr.Content = Testers.ContainsExpression("200", "expected 200 OK response")
tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression("success!", "expected success")
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr2 = Test.AddTestRun("Update config files")
# Update the multicert config
sslcertpath = ts.Disk.ssl_multicert_config.AbsPath
tr2.Disk.File(sslcertpath, id="ssl_multicert_config", typename="ats:config"),
tr2.Disk.ssl_multicert_config.AddLines(
    [
        'dest_ip=* ssl_cert_name=passphrase2.pem ssl_key_name=passphrase2.key ssl_key_dialog="exec:/bin/bash -c \'echo -n passphrase\'"',
    ])
tr2.StillRunningAfter = ts
tr2.StillRunningAfter = server
tr2.Processes.Default.Command = 'echo Updated configs'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr2.Processes.Default.Env = ts.Env
tr2.Processes.Default.ReturnCode = 0

tr2reload = Test.AddTestRun("Reload config")
tr2reload.StillRunningAfter = ts
tr2reload.StillRunningAfter = server
tr2reload.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr2reload.Processes.Default.Env = ts.Env
tr2reload.Processes.Default.ReturnCode = 0

tr3 = Test.AddTestRun("use a key with passphrase")
tr3.Setup.Copy("ssl/signer.pem")
tr3.Processes.Default.Command = f"curl -v --cacert ./signer.pem  --resolve 'passphrase:{ts.Variables.ssl_port}:127.0.0.1' https://passphrase:{ts.Variables.ssl_port}/"
tr3.ReturnCode = 0
tr3.Processes.Default.Streams.stderr.Content = Testers.ContainsExpression("200", "expected 200 OK response")
tr3.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression("success!", "expected success")
tr3.StillRunningAfter = server
tr3.StillRunningAfter = ts
