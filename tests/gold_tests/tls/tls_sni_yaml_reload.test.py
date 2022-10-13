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
Test reloading sni.yaml behaves as expected
'''

sni_domain = 'example.com'

ts = Test.MakeATSProcess("ts", command="traffic_manager", enable_tls=True)
server = Test.MakeOriginServer("server")
server2 = Test.MakeOriginServer("server3")
request_header = {"headers": f"GET / HTTP/1.1\r\nHost: {sni_domain}\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
    'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
    'proxy.config.ssl.CA.cert.filename': f'{ts.Variables.SSLDir}/signer.pem',
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl|http',
    'proxy.config.diags.output.debug': 'L',
})


ts.addDefaultSSLFiles()
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/signer.pem")

ts.Disk.remap_config.AddLine(
    f'map / http://127.0.0.1:{server.Variables.Port}'
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)


ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.sni_yaml.AddLines(
    f"""
      sni:
      - fqdn: {sni_domain}
        http2: off
        client_cert: {ts.Variables.SSLDir}/signed-foo.pem
        client_key: {ts.Variables.SSLDir}/signed-foo.key
        verify_client: STRICT
      """.split('\n')
)

tr = Test.AddTestRun(f'ensure we can connect for SNI {sni_domain}')
tr.Setup.Copy("ssl/signed-foo.pem")
tr.Setup.Copy("ssl/signed-foo.key")
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(server)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = f"curl -q --tls-max 1.2 -s -v -k  --cert ./signed-foo.pem --key ./signed-foo.key --resolve '{sni_domain}:{ts.Variables.ssl_port}:127.0.0.1' https://{sni_domain}:{ts.Variables.ssl_port}"

tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Verify curl could successfully connect")
tr.Processes.Default.Streams.stderr = Testers.IncludesExpression(f"CN={sni_domain}", f"Verify curl used the {sni_domain} SNI")
ts.Disk.diags_log.Content = Testers.IncludesExpression(
    "SSL negotiation finished successfully",
    "Verify that the TLS handshake was successful")

# This config reload should fail because it references non-existent TLS key files
trupd = Test.AddTestRun("Update config file")
# Update the configs - this will overwrite the sni.yaml file
sniyamlpath = ts.Disk.sni_yaml.AbsPath
trupd.Disk.File(sniyamlpath, id="sni_yaml", typename="ats:config")
trupd.Disk.sni_yaml.AddLines(
    f"""
      sni:
      - fqdn: {sni_domain}
        http2: on
        client_cert: {ts.Variables.SSLDir}/signed-notexist.pem
        client_key: {ts.Variables.SSLDir}/signed-notexist.key
        verify_client: STRICT
      """.split('\n')
)

trupd.StillRunningAfter = ts
trupd.StillRunningAfter = server
trupd.Processes.Default.Command = 'echo Updated configs'
trupd.Processes.Default.Env = ts.Env
trupd.Processes.Default.ReturnCode = 0


tr2reload = Test.AddTestRun("Reload config")
tr2reload.StillRunningAfter = ts
tr2reload.StillRunningAfter = server
tr2reload.Processes.Default.Command = 'traffic_ctl config reload'
tr2reload.Processes.Default.Env = ts.Env
tr2reload.Processes.Default.ReturnCode = 0
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    'sni.yaml failed to load',
    'reload should result in failure to load sni.yaml')

tr3 = Test.AddTestRun(f"Make request again for {sni_domain} that should still work")
# Wait for the reload to complete
tr3.Setup.Copy("ssl/signed-bar.pem")
tr3.Setup.Copy("ssl/signed-bar.key")
tr3.Processes.Default.StartBefore(server2, ready=When.FileContains(ts.Disk.diags_log.Name, "signed-notexist.pem", 1))
tr3.StillRunningAfter = ts
tr3.StillRunningAfter = server
tr3.Processes.Default.Command = f"curl -q --tls-max 1.2 -s -v -k  --cert ./signed-bar.pem --key ./signed-bar.key --resolve '{sni_domain}:{ts.Variables.ssl_port}:127.0.0.1' https://{sni_domain}:{ts.Variables.ssl_port}"

tr3.Processes.Default.ReturnCode = 0
# since the 2nd config with http2 turned on should have failed and used the prior config, verify http2 was not used
tr3.Processes.Default.Streams.stderr = Testers.ExcludesExpression("GET / HTTP/2", "Confirm that HTTP2 is still not used")
