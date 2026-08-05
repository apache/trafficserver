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
import sys

Test.Summary = '''
Abort TLS handshakes while an OpenSSL async job is mid-pause, to exercise the
SSLNetVConnection teardown path. When a handshake returns SSL_ERROR_WANT_ASYNC
an eventfd is registered on the poller with the connection as its target, and a
connection torn down before the async job finishes must deregister that eventfd
so the poller is not left pointing at a freed connection. Built under ASan the
server must survive the abort barrage with no sanitizer error and still serve a
normal request.
'''

async_handshake = os.path.join(Test.Variables.AtsTestPluginsDir, 'async_handshake.so')

Test.SkipUnless(
    Condition.HasOpenSSLVersion('1.1.1'),
    Condition.IsOpenSSL(),
    Condition(lambda: os.path.isfile(async_handshake), async_handshake + " not found."),
)

ts = Test.MakeATSProcess("ts", enable_tls=True)
server = Test.MakeOriginServer("server")

# A wide pause window (well beyond the abort client's 0.4s handshake attempt)
# so the abort reliably lands while the async job is still in flight.
if os.path.isfile(async_handshake):
    Test.PrepareTestPlugin(async_handshake, ts, '-delay-ms=2000')

server.addResponse(
    "sessionlog.json", {
        "headers": "GET / HTTP/1.1\r\nuuid: basic\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }, {
        "headers":
            "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\nCache-Control: max-age=3600\r\nContent-Length: 2\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "ok"
    })

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.ssl.async.handshake.enabled': 1,
        'proxy.config.diags.debug.enabled': 0,
        'proxy.config.diags.debug.tags': 'ssl'
    })

# Fire a barrage of handshakes that abort while the async job is mid-pause. Correct
# teardown is validated by ATS surviving this with no crash and, under ASan, no
# sanitizer error. Without deregistration the connection is freed while its
# eventfd still has a live poller registration.
abort_client = os.path.join(Test.TestDirectory, 'tls_engine_abort.py')

tr = Test.AddTestRun("abort-during-async-handshake")
tr.Processes.Default.Command = "{0} {1} {2} 30".format(sys.executable, abort_client, ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.Processes.Default.Streams.All = Testers.ContainsExpression("sent 30 aborted handshakes", "Abort client ran")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# After the abort barrage, a normal request must still succeed: the server is
# healthy, not crashed or wedged.
tr2 = Test.AddTestRun("normal-request-after-aborts")
tr2.MakeCurlCommand("-k -v -H uuid:basic -H host:example.com https://127.0.0.1:{0}/".format(ts.Variables.ssl_port), ts=ts)
tr2.ReturnCode = 0
tr2.Processes.Default.Streams.All = Testers.ContainsExpression(r"HTTP/(2|1\.1) 200", "Request succeeds after the abort barrage")
tr2.StillRunningAfter = ts
tr2.StillRunningAfter = server

# The abort barrage must actually drive handshakes into the async pause,
# otherwise the eventfd is never registered and the test proves nothing. The
# async_handshake plugin's wake thread prints this to stderr (-> traffic.out)
# when it signals the eventfd at the end of its pause; that only happens if a
# handshake entered the WANT_ASYNC path and armed the eventfd, so its presence
# confirms the teardown path was exercised (and that the async job completed
# after the abort -- exactly the use-after-free window this fix closes).
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "sent async wake signal to", "Async job engaged on at least one handshake")

# The server process must not have reported an AddressSanitizer error. ASan
# writes to stderr, which the harness binds to traffic.out -- not diags.log --
# so the exclusion has to be checked against traffic.out to catch the UAF.
ts.Disk.traffic_out.Content += Testers.ExcludesExpression("AddressSanitizer", "No ASan error in the server")
