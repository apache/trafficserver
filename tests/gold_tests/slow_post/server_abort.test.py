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
AuTest with bad configuration of microserver to simulate server aborting the connection unexpectedly
'''
ts = Test.MakeATSProcess("ts", enable_tls=True)
# note the microserver by default is not configured to use ssl
server = Test.MakeOriginServer("server")
ts.Disk.remap_config.AddLine(
    # The following config tells ATS to do tls with the origin server on a
    # non-tls port. This is misconfigured intentionally to trigger an exception
    # on the origin server so that it aborts the connection upon receiving a
    # request
    'map / https://127.0.0.1:{0}'.format(server.Variables.Port))
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=aaa-signed.pem ssl_key_name=aaa-signed.key')
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.tags': 'http|dns',
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.ssl.server.cert.path': f'{Test.TestDirectory}/test_secrets',
        'proxy.config.ssl.server.private_key.path': f'{Test.TestDirectory}/test_secrets',
    })

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.MakeCurlCommand("-v -k -H \")host: foo.com\" https://127.0.0.1:{0}".format(ts.Variables.ssl_port))
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
server.Streams.stderr += Testers.ContainsExpression(
    "UnicodeDecodeError", "Verify that the server raises an exception when processing the request.")
