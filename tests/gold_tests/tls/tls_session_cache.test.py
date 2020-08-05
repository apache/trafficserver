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

import re
Test.Summary = '''
Test tls session cache
'''

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")


# Add info the origin server responses
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.session_cache': 2,
    'proxy.config.ssl.session_cache.size': 4096,
    'proxy.config.ssl.session_cache.num_buckets': 256,
    'proxy.config.ssl.session_cache.skip_cache_on_bucket_contention': 0,
    'proxy.config.ssl.session_cache.timeout': 0,
    'proxy.config.ssl.session_cache.auto_clear': 1,
    'proxy.config.ssl.server.session_ticket.enable': 0,
})

# Check that Session-ID is the same on every connection


def checkSession(ev):
    retval = False
    f = open(openssl_output, 'r')
    err = "Session ids match"
    if not f:
        err = "Failed to open {0}".format(openssl_output)
        return (retval, "Check that session ids match", err)

    content = f.read()
    match = re.findall('Session-ID: ([0-9A-F]+)', content)

    if match:
        if all(i == j for i, j in zip(match, match[1:])):
            err = "{0} reused successfully {1} times".format(match[0], len(match))
            retval = True
        else:
            err = "Session is not being reused as expected"
    else:
        err = "Didn't find session id"
    return (retval, "Check that session ids match", err)


tr = Test.AddTestRun("OpenSSL s_client -reconnect")
tr.Command = 'echo -e "GET / HTTP/1.0\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -reconnect'.format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
# time delay as proxy.config.http.wait_for_cache could be broken
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
openssl_output = tr.Processes.Default.Streams.stdout.AbsPath
tr.Processes.Default.Streams.All.Content = Testers.Lambda(checkSession)
tr.StillRunningAfter = server
