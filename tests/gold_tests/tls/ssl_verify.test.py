#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  'License'); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an 'AS IS' BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import os

Test.Summary = '''
Test SSL verify callback by checking DNS record in Subject Alternative Name
'''

Test.SkipUnless(
    Condition.HasProgram('curl', 'Curl need to be installed on system for this test to work')
)

SERVER_NAME_IN_CERT = 'server.ats.test'
# the synthetic server has different domain name, which mismatches the DNS record in the server cert
SYNTH_SERVER_NAME = 'synthetic.ats.test'

ts = Test.MakeATSProcess('ts')
server = Test.MakeOriginServer("server", ssl=True, options={
    '--cert': '{0}/{1}.pem'.format(ts.Variables.SSLDir, SERVER_NAME_IN_CERT),
    '--key': '{0}/{1}.key'.format(ts.Variables.SSLDir, SERVER_NAME_IN_CERT),
})

request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993"}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993"}
server.addResponse("sessionfile.log", request_header, response_header)

ts.addSSLfile("ssl/ca.pem")
ts.addSSLfile("ssl/ca.key")
ts.addSSLfile("ssl/{0}.pem".format(SERVER_NAME_IN_CERT))
ts.addSSLfile("ssl/{0}.key".format(SERVER_NAME_IN_CERT))

dns = Test.MakeDNServer("dns")
dns.addRecords(records = {
    SERVER_NAME_IN_CERT: ['127.0.0.1'],
    SYNTH_SERVER_NAME: ['127.0.0.1'],
})

ts.Disk.records_config.update({
    # reduce the number of unnecessary retries in negative case
    'proxy.config.http.connect_attempts_max_retries': 0,
    # CA cert is required to pass the positive case
    'proxy.config.ssl.client.CA.cert.path': '{}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.CA.cert.filename': 'ca.pem',
    'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
    'proxy.config.dns.resolv_conf': 'NULL',
})

ts.Disk.remap_config.AddLine(
    'map /positive https://{0}:{1} @plugin=ssl_verify_remap.so @pparam={0}'.format(SERVER_NAME_IN_CERT, server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    'map /negative https://{0}:{1} @plugin=ssl_verify_remap.so @pparam={0}'.format(SYNTH_SERVER_NAME, server.Variables.Port)
)

ts.Disk.diags_log.Content = Testers.ContainsExpression('ERROR:', 'SSL3_GET_SERVER_CERTIFICATE:certificate verify failed')

Test.PreparePlugin(os.path.join(Test.Variables.AtsTestToolsDir, 'plugins', 'ssl_verify_remap.cc'), ts)

tr = Test.AddTestRun('Positive')
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.Command = 'curl -sf -o /dev/null http://127.0.0.1:{0}/positive'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.StillRunningAfter = dns

tr = Test.AddTestRun('Negative')
tr.Processes.Default.Command = 'curl -sf -o /dev/null http://127.0.0.1:{0}/negative'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 22
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.StillRunningAfter = dns
