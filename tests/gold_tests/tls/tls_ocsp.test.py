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
Test tls server prefetched OCSP responses
'''

Test.SkipUnless(
    Condition.HasCurlVersion("7.41.0"),  # curl --cert-status option has been introduced in version 7.41.0
    Condition.IsOpenSSL(),  # functionality tested in this test requires OpenSSL specific APIs
)

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")
request_header = {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# desired response form the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addSSLfile("ssl/ca.ocsp.pem")
ts.addSSLfile("ssl/server.ocsp.pem")
ts.addSSLfile("ssl/server.ocsp.key")
ts.addSSLfile("ssl/ocsp_response.der")

ts.Disk.remap_config.AddLine(
    'map https://example.com:{0} http://127.0.0.1:{1}'.format(ts.Variables.ssl_port, server.Variables.Port)
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.ocsp.pem ssl_key_name=server.ocsp.key ssl_ocsp_name=ocsp_response.der'
)

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.cert_chain.filename': 'ca.ocsp.pem',
    # enable prefetched OCSP responses
    'proxy.config.ssl.ocsp.response.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.ocsp.enabled': 1,
    'proxy.config.exec_thread.autoconfig.scale': 1.0
})


tr = Test.AddTestRun("Check OCSP response using curl")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Command = "curl -v --cacert {0} --cert-status -H \"host:example.com\" https://127.0.0.1:{1}".format(
    os.path.join(ts.Variables.SSLDir, "ca.ocsp.pem"), ts.Variables.ssl_port)
tr.ReturnCode = 0
