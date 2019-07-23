'''
Test the client_context_dump plugin.
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
Test client_context_dump plugin
'''

# Set up ATS
ts = Test.MakeATSProcess("ts", command="traffic_manager", select_ports=True, enable_tls=True)

# Set up ssl files
ts.addSSLfile("ssl/one.com.pem")
ts.addSSLfile("ssl/two.com.pem")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'client_context_dump',
    'proxy.config.ssl.server.cert.path': '{}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.cert.path': '{}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.private_key.path': '{}'.format(ts.Variables.SSLDir),
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=one.com.pem ssl_key_name=one.com.pem'
)

ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: "*one.com"',
    '  client_cert: "one.com.pem"',
    '- fqdn: "*two.com"',
    '  client_cert: "two.com.pem"'
])

# Set up plugin
Test.PreparePlugin(Test.Variables.AtsExampleDir + '/plugins/c-api/client_context_dump/client_context_dump.cc', ts)

# custom log comparison
Test.Disk.File(ts.Variables.LOGDIR + '/client_context_dump.log', exists=True, content='gold/client_context_dump.gold')

# traffic server test
t = Test.AddTestRun("Test traffic server started properly")
t.StillRunningAfter = Test.Processes.ts

p = t.Processes.Default
p.Command = "curl http://127.0.0.1:{0}".format(ts.Variables.port)
p.ReturnCode = 0
p.StartBefore(Test.Processes.ts)

# Client contexts test
tr = Test.AddTestRun()
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = (
    '{0}/traffic_ctl plugin msg client_context_dump.t 1'.format(ts.Variables.BINDIR)
)
tr.Processes.Default.ReturnCode = 0
