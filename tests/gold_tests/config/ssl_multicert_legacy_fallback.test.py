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
Verify Apache Traffic Server falls back to legacy ssl_multicert.config
when ssl_multicert.yaml is absent.
'''

sni_domain = 'example.com'

ts = Test.MakeATSProcess("ts", enable_tls=True, use_legacy_ssl_multicert=True)
server = Test.MakeOriginServer("server")
request_header = {"headers": f"GET / HTTP/1.1\r\nHost: {sni_domain}\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
    })

ts.addDefaultSSLFiles()

ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}')

# Stage a legacy ssl_multicert.config (line-based) instead of ssl_multicert.yaml.
legacy_path = os.path.join(ts.Variables.CONFIGDIR, 'ssl_multicert.config')
ts.Disk.File(legacy_path, id='ssl_multicert_config', typename='ats:config')
ts.Disk.ssl_multicert_config.AddLines([
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key',
])

# The fallback Note should appear in diags.log.
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    r'ssl_multicert\.yaml not found, falling back to ssl_multicert\.config',
    'ssl_multicert.yaml fallback Note should be logged when only the legacy file is present')

tr = Test.AddTestRun(f"Connect using cert loaded from legacy ssl_multicert.config (SNI={sni_domain})")
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(server)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_domain}:{ts.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_domain}:{ts.Variables.ssl_port}",
    ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
tr.Processes.Default.Streams.stderr = Testers.IncludesExpression(f"CN={sni_domain}", "Check response")

##########################################################################
# When ssl_multicert.yaml exists alongside legacy ssl_multicert.config,
# yaml wins and a Note about the legacy file being ignored is logged.
ts2 = Test.MakeATSProcess("ts2", enable_tls=True)
server2 = Test.MakeOriginServer("server2")
server2.addResponse("sessionlog.json", request_header, response_header)

ts2.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts2.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts2.Variables.SSLDir}',
    })
ts2.addDefaultSSLFiles()
ts2.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server2.Variables.Port}')

ts2.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

# Stage a stray legacy file so we exercise the "both present" warning path.
legacy2_path = os.path.join(ts2.Variables.CONFIGDIR, 'ssl_multicert.config')
ts2.Disk.File(legacy2_path, id='ssl_multicert_config_extra', typename='ats:config')
ts2.Disk.ssl_multicert_config_extra.AddLines([
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key',
])

ts2.Disk.diags_log.Content += Testers.ContainsExpression(
    r'ssl_multicert\.config exists alongside ssl_multicert\.yaml; the legacy file is ignored',
    'Note about ignored legacy ssl_multicert.config should be logged when both files are present')

tr2 = Test.AddTestRun(f"yaml wins when both files exist (SNI={sni_domain})")
tr2.Processes.Default.StartBefore(ts2)
tr2.Processes.Default.StartBefore(server2)
tr2.StillRunningAfter = ts2
tr2.StillRunningAfter = server2
tr2.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_domain}:{ts2.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_domain}:{ts2.Variables.ssl_port}",
    ts=ts2)
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
tr2.Processes.Default.Streams.stderr = Testers.IncludesExpression(f"CN={sni_domain}", "Check response")

##########################################################################
# When the admin has customized proxy.config.ssl.server.multicert.filename
# to something other than the default ssl_multicert.yaml, the legacy
# fallback must NOT engage even if ssl_multicert.config exists.
ts3 = Test.MakeATSProcess("ts3", enable_tls=True, use_legacy_ssl_multicert=True)
server3 = Test.MakeOriginServer("server3")
server3.addResponse("sessionlog.json", request_header, response_header)

ts3.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts3.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts3.Variables.SSLDir}',
        'proxy.config.ssl.server.multicert.filename': 'custom_multicert.yaml',
    })

ts3.addDefaultSSLFiles()
ts3.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server3.Variables.Port}')

# Stage the custom-named YAML so ATS has something valid to load.
custom_path = os.path.join(ts3.Variables.CONFIGDIR, 'custom_multicert.yaml')
ts3.Disk.File(custom_path, id='ssl_multicert_yaml_custom', typename='ats:config')
ts3.Disk.ssl_multicert_yaml_custom.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

# Also stage a legacy ssl_multicert.config that would normally trigger fallback —
# but must be ignored here because the record was customized.
legacy3_path = os.path.join(ts3.Variables.CONFIGDIR, 'ssl_multicert.config')
ts3.Disk.File(legacy3_path, id='ssl_multicert_config_custom', typename='ats:config')
ts3.Disk.ssl_multicert_config_custom.AddLines([
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key',
])

# Neither the fallback Note nor the "both present" Note should appear, because the
# record value is not the default.
ts3.Disk.diags_log.Content += Testers.ExcludesExpression(
    r'ssl_multicert\.yaml not found, falling back to ssl_multicert\.config',
    'Fallback Note must NOT appear when record is at a non-default filename')
ts3.Disk.diags_log.Content += Testers.ExcludesExpression(
    r'ssl_multicert\.config exists alongside ssl_multicert\.yaml; the legacy file is ignored',
    '"Both present" Note must NOT appear when record is at a non-default filename')

tr3 = Test.AddTestRun(f"Custom record value disables legacy fallback (SNI={sni_domain})")
tr3.Processes.Default.StartBefore(ts3)
tr3.Processes.Default.StartBefore(server3)
tr3.StillRunningAfter = ts3
tr3.StillRunningAfter = server3
tr3.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_domain}:{ts3.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_domain}:{ts3.Variables.ssl_port}",
    ts=ts3)
tr3.Processes.Default.ReturnCode = 0
tr3.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
tr3.Processes.Default.Streams.stderr = Testers.IncludesExpression(f"CN={sni_domain}", "Check response")
