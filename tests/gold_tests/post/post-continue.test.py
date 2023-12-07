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

# ----
# Setup Test
# ----
Test.Summary = '''
Test the Expect header in post
'''
# Require HTTP/2 enabled Curl
Test.SkipUnless(Condition.HasCurlFeature('http2'),)
Test.ContinueOnFail = True

# ----
# Setup httpbin Origin Server
# ----
replay_file = "replay/post-continue.replay.yaml"
server = Test.MakeVerifierServerProcess("server", replay_file)

# ----
# Setup ATS
# ----
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True, enable_cache=False)

ts2 = Test.MakeATSProcess("ts2", select_ports=True, enable_tls=True, enable_cache=False)

# add ssl materials like key, certificates for the server
ts.addDefaultSSLFiles()
ts2.addDefaultSSLFiles()

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
    })
ts2.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.http_port))
ts2.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
ts2.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.diags.debug.enabled': 0,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.send_100_continue_response': 1
    })

big_post_body = "0123456789" * 131070
big_post_body_file = open(os.path.join(Test.RunDirectory, "big_post_body"), "w")
big_post_body_file.write(big_post_body)
big_post_body_file.close()

test_run = Test.AddTestRun("http1.1 POST small body with Expect header")
test_run.Processes.Default.StartBefore(server)
test_run.Processes.Default.StartBefore(ts)
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http1.1 -H "uuid: post" -H "Expect: 100-continue" -d "small body" -k https://127.0.0.1:{0}/post'.format(
    ts.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h1.gold"
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 100 continue", "Has Expect header")
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("Expect: 100-continue", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http1.1 POST large body with Expect header")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http1.1 -H "uuid: post" -H "Expect: 100-continue" -d @big_post_body -k https://127.0.0.1:{0}/post'.format(
    ts.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h1.gold"
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 100 continue", "Has Expect header")
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("Expect: 100-continue", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http1.1 POST small body w/o Expect header")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http1.1 -H "uuid: post" -H "Expect:" -d "small body" -k https://127.0.0.1:{0}/post'.format(
    ts.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h1.gold"
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("HTTP/1.1 100 continue", "Does not have Expect header")
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("Expect: 100-continue", "Does not have Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http1.1 POST large body w/o Expect header")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http1.1 -H "uuid: post" -H "Expect: " -d @big_post_body -k https://127.0.0.1:{0}/post'.format(
    ts.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h1.gold"
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("HTTP/1.1 100 continue", "Does not have Expect header")
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("Expect: 100-continue", "Does not have Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http2 POST small body with Expect header")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http2 -H "uuid: post" -H "Expect: 100-continue" -d "small body" -k https://127.0.0.1:{0}/post'.format(
    ts.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h2.gold"
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("xpect: 100-continue", "Has expect header")
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/2 100", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http2 POST large body with Expect header")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http2 -H "uuid: post" -H "Expect: 100-continue" -d @big_post_body -k https://127.0.0.1:{0}/post'.format(
    ts.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h2.gold"
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("xpect: 100-continue", "Has expect header")
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/2 100", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http2 POST small body w/o Expect header")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http2 -H "uuid: post" -H "Expect: " -d "small body" -k https://127.0.0.1:{0}/post'.format(
    ts.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h2.gold"
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("xpect: 100-continue", "Has expect header")
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("HTTP/2 100", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http2 POST large body w/o Expect header")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http2 -H "uuid: post" -H "Expect: " -d @big_post_body -k https://127.0.0.1:{0}/post'.format(
    ts.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h2.gold"
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("xpect: 100-continue", "Has expect header")
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("HTTP/2 100", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts
test_run.Processes.Default.ReturnCode = 0

# Do them all again against the TS that will return 100-continue immediately
test_run = Test.AddTestRun("http1.1 POST small body with Expect header, immediate")
test_run.Processes.Default.StartBefore(Test.Processes.ts2)
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http1.1 -H "uuid: post" -H "Expect: 100-continue" -d "small body" -k https://127.0.0.1:{0}/post'.format(
    ts2.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h1.gold"
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 100 Continue", "Has Expect header")
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("Expect: 100-continue", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts2
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http1.1 POST large body with Expect header, immediate")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http1.1 -H "uuid: post" -H "Expect: 100-continue" -d @big_post_body -k https://127.0.0.1:{0}/post'.format(
    ts2.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h1.gold"
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 100 Continue", "Has Expect header")
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("Expect: 100-continue", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts2
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http1.1 POST small body w/o Expect header, immediate")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http1.1 -H "uuid: post" -H "Expect:" -d "small body" -k https://127.0.0.1:{0}/post'.format(
    ts2.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h1.gold"
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("HTTP/1.1 100 Continue", "Has Expect header")
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("Expect 100-continue", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts2
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http1.1 POST large body w/o Expect header, immediate")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http1.1 -H "uuid: post" -H "Expect: " -d @big_post_body -k https://127.0.0.1:{0}/post'.format(
    ts2.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h1.gold"
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("HTTP/1.1 100 Continue", "Has Expect header")
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("Expect 100-continue", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts2
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http2 POST small body with Expect header, immediate")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http2 -H "uuid: post" -H "Expect: 100-continue" -d "small body" -k https://127.0.0.1:{0}/post'.format(
    ts2.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h2.gold"
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("xpect: 100-continue", "Has expect header")
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/2 100", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts2
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http2 POST large body with Expect header, immediate")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http2 -H "uuid: post" -H "Expect: 100-continue" -d @big_post_body -k https://127.0.0.1:{0}/post'.format(
    ts2.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h2.gold"
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("xpect: 100-continue", "Has expect header")
test_run.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/2 100", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts2
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http2 POST small body w/o Expect header, immediate")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http2 -H "uuid: post" -H "Expect: " -d "small body" -k https://127.0.0.1:{0}/post'.format(
    ts2.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h2.gold"
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("xpect: 100-continue", "Has expect header")
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("HTTP/2 100", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts2
test_run.Processes.Default.ReturnCode = 0

test_run = Test.AddTestRun("http2 POST large body w/o Expect header, immediate")
test_run.Processes.Default.Command = 'curl -v -o /dev/null --http2 -H "uuid: post" -H "Expect: " -d @big_post_body -k https://127.0.0.1:{0}/post'.format(
    ts2.Variables.ssl_port)
test_run.Processes.Default.Streams.All = "gold/post-h2.gold"
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("xpect: 100-continue", "Has expect header")
test_run.Processes.Default.Streams.All += Testers.ExcludesExpression("HTTP/2 100", "Has Expect header")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts2
test_run.Processes.Default.ReturnCode = 0
