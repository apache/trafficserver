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
import subprocess
Test.Summary = '''
Test url_sig plugin
'''

Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_TLS_ALPN'),
)
Test.ContinueOnFail = True

# Skip if plugins not present.
Test.SkipUnless(Condition.PluginExists('url_sig.so'))
Test.SkipUnless(Condition.PluginExists('balancer.so'))

# Set up to check the output after the tests have run.
#
url_sig_log_id = Test.Disk.File("url_sig_short.log")
url_sig_log_id.Content = "url_sig.gold"

server = Test.MakeOriginServer("server")

request_header = {
    "headers": "GET /foo/abcde/qrstuvwxyz HTTP/1.1\r\nHost: just.any.thing\r\n\r\n", "timestamp": "1469733493.993", "body": ""
}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=False)

ts.addSSLfile("../../remap/ssl/server.pem")
ts.addSSLfile("../../remap/ssl/server.key")

ts.Variables.ssl_port = 4443

ts.Disk.records_config.update({
    # 'proxy.config.diags.debug.enabled': 1,
    # 'proxy.config.diags.debug.tags': 'http|url_sig',
    'proxy.config.http.cache.http': 0,  # Make sure each request is forwarded to the origin server.
    'proxy.config.proxy_name': 'Poxy_Proxy',  # This will be the server name.
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.server_ports': (
        'ipv4:{0} ipv4:{1}:proto=http:ssl'.format(ts.Variables.port, ts.Variables.ssl_port))
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Use unchanged incoming URL.
#
ts.Disk.remap_config.AddLine(
    'map http://one.two.three/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=url_sig.so @pparam={}/url_sig.config'.format(Test.TestDirectory)
)

# Use unchanged incoming HTTPS URL.
#
ts.Disk.remap_config.AddLine(
    'map https://one.two.three/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=url_sig.so @pparam={}/url_sig.config'.format(Test.TestDirectory)
)

# Use pristine URL, incoming URL unchanged.
#
ts.Disk.remap_config.AddLine(
    'map http://four.five.six/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=url_sig.so @pparam={}/url_sig.config @pparam=pristineurl'.format(Test.TestDirectory)
)

# Use pristine URL, incoming URL changed.
#
ts.Disk.remap_config.AddLine(
    'map http://seven.eight.nine/ http://dummy' +
    ' @plugin=balancer.so @pparam=--policy=hash,url @pparam=127.0.0.1:{}'.format(server.Variables.Port) +
    ' @plugin=url_sig.so @pparam={}/url_sig.config @pparam=PristineUrl'.format(Test.TestDirectory)
)

# Validation failure tests.

LogTee = " 2>&1 | grep '^<' | tee -a {}/url_sig_long.log".format(Test.RunDirectory)

# Bad client / MD5 / P=101 / URL pristine / URL altered.
#
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.2&E=33046620008&A=2&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8'" +
    LogTee
)

# With client / MD5 / P=010 / URL pristine / URL altered -- Expired.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.1&E=1&A=2&K=13&P=010&S=f237aad1fa010234d7bf8108a0e36387'" +
    LogTee
)

# With client / No algorithm / P=101 / URL pristine / URL altered.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.1&E=33046620008&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8'" +
    LogTee
)

# With client / Bad algorithm / P=101 / URL pristine / URL altered.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.1&E=33046620008&A=3&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8'" +
    LogTee
)

# With client / MD5 / No parts / URL pristine / URL altered.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.1&E=33046620008&A=2&K=13&S=d1f352d4f1d931ad2f441013402d93f8'" +
    LogTee
)

# With client / MD5 / P=10 (bad) / URL pristine / URL altered.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.1&E=33046620008&A=2&K=13&P=10&S=d1f352d4f1d931ad2f441013402d93f8'" +
    LogTee
)

# With client / MD5 / P=101 / URL pristine / URL altered -- No signature.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.1&E=33046620008&A=2&K=13&P=101'" +
    LogTee
)

# With client / MD5 / P=101 / URL pristine / URL altered  -- Bad signature.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.1&E=33046620008&A=2&K=13&P=101&S=d1f452d4f1d931ad2f441013402d93f8'" +
    LogTee
)

# With client / MD5 / P=101 / URL pristine / URL altered -- Spurious &.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.1&E=33046620008&A=2&&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8#'" +
    LogTee
)

# Success tests.

# No client / SHA1 / P=1 / URL not pristine / URL not altered.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://one.two.three/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?E=33046618506&A=1&K=7&P=1&S=acae22b0e1ba6ea6fbb5d26018dbf152558e98cb'" +
    LogTee
)

# With client / SHA1 / P=1 / URL pristine / URL not altered.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://four.five.six/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.1&E=33046618556&A=1&K=15&P=1&S=f4103561a23adab7723a89b9831d77e0afb61d92'" +
    LogTee
)

# No client / MD5 / P=1 / URL pristine / URL altered.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?E=33046618586&A=2&K=0&P=1&S=0364efa28afe345544596705b92d20ac'" +
    LogTee
)

# With client / MD5 / P=010 / URL pristine / URL altered.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.1&E=33046619717&A=2&K=13&P=010&S=f237aad1fa010234d7bf8108a0e36387'" +
    LogTee
)

# With client / MD5 / P=101 / URL pristine / URL altered.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --proxy http://127.0.0.1:{} 'http://seven.eight.nine/".format(ts.Variables.port) +
    "foo/abcde/qrstuvwxyz?C=127.0.0.1&E=33046620008&A=2&K=13&P=101&S=d1f352d4f1d931ad2f441013402d93f8'" +
    LogTee
)

# No client / SHA1 / P=1 / URL not pristine / URL not altered -- HTTPS.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    "curl --verbose --insecure --header 'Host: one.two.three' 'https://127.0.0.1:{}/".format(ts.Variables.ssl_port) +
    "foo/abcde/qrstuvwxyz?E=33046618506&A=1&K=7&P=1&S=acae22b0e1ba6ea6fbb5d26018dbf152558e98cb'" +
    LogTee + " ; grep -F -e '< HTTP' -e Authorization {0}/url_sig_long.log > {0}/url_sig_short.log ".format(ts.RunDirectory)
)
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5

# Overriding the built in ERROR check since we expect some ERROR messages
ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR", "Some tests are failure tests")
