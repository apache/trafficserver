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
Test the reported type of HTTP transactions and tunnels
'''

# Define default ATS. Disable the cache to simplify the test.
ts = Test.MakeATSProcess("ts", enable_cache=False, enable_tls=True)
ts.addSSLfile("../tls/ssl/server.pem")
ts.addSSLfile("../tls/ssl/server.key")

server = Test.MakeOriginServer("server", ssl=True)
server2 = Test.MakeOriginServer("server2")

Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: http-test\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length:0\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
request_tunnel_header = {"headers": "GET / HTTP/1.1\r\nHost: tunnel-test\r\n\r\n",
                         "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_tunnel_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length:0\r\n\r\n",
                          "timestamp": "1469733493.993", "body": ""}


Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir,
                                    'hook_tunnel_plugin.so'), ts)

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)
server.addResponse("sessionfile.log", request_tunnel_header, response_tunnel_header)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|test',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
    'proxy.config.http.connect_ports': '{0}'.format(server.Variables.SSL_Port)
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map https://http-test:{0}/ https://127.0.0.1:{1}/'.format(
        ts.Variables.ssl_port, server.Variables.SSL_Port)
)

ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: tunnel-test',
    "  tunnel_route: localhost:{0}".format(server.Variables.SSL_Port),
])

# Add connection close to ensure that the client connection closes promptly after completing the transaction
cmd_http = 'curl -k --http1.1 -H "Connection: close" -vs --resolve "http-test:{0}:127.0.0.1" https://http-test:{0}/'.format(
    ts.Variables.ssl_port)
cmd_tunnel = 'curl -k --http1.1 -H "Connection: close" -vs --resolve "tunnel-test:{0}:127.0.0.1"  https://tunnel-test:{0}/'.format(
    ts.Variables.ssl_port)
cmd_connect = 'curl -k --http1.1 -H "Connection: close" -vs --resolve "connect-proxy:{0}:127.0.0.1" -x http://connect-proxy:{0} --resolve "http-test:{1}:127.0.0.1"  https://http-test:{1}/'.format(
    ts.Variables.port, ts.Variables.ssl_port)

# Send the http request
tr = Test.AddTestRun("send http request")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = cmd_http
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.SSL_Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Send the tunnel request
tr = Test.AddTestRun("send tunnel request")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = cmd_tunnel
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Send the connect request
# while the connect method will set up a tunnel, it is processed in ATS as a
# transaction rather than a blind tunnel directly. Plugs can differentiate on the
# method to determine whether a connect tunnel will be set up
tr = Test.AddTestRun("send connect request")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = cmd_connect
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Signal that all the curl processes have completed
tr = Test.AddTestRun("Curl Done")
tr.DelayStart = 2  # Delaying a couple seconds to make sure the global continuation's lock contention resolves.
tr.Processes.Default.Command = "traffic_ctl plugin msg done done"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Parking this as a ready tester on a meaningless process
# To stall the test runs that check for the stats until the
# stats have propagated and are ready to read.


def make_done_stat_ready(tsenv):
    def done_stat_ready(process, hasRunFor, **kw):
        retval = subprocess.run(
            "traffic_ctl metric get txn_type_verify.test.done",
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=tsenv)
        return b'1' in retval.stdout

    return done_stat_ready


# number of sessions/transactions opened and closed are equal
tr = Test.AddTestRun("Check type errors")
server2.StartupTimeout = 60
# Again, here the important thing is the ready function not the server2 process
tr.Processes.Default.StartBefore(server2, ready=make_done_stat_ready(ts.Env))
tr.Processes.Default.Command = 'traffic_ctl metric get txn_type_verify.error'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'txn_type_verify.error 0', 'incorrect statistic return, or possible error.')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("Check for tunnel start")
tr.Processes.Default.Command = 'traffic_ctl metric get txn_type_verify.tunnel.start'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "txn_type_verify.tunnel.start 1", 'Should have a tunnel start.')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("Check for http request")
tr.Processes.Default.Command = 'traffic_ctl metric get txn_type_verify.http.req'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "txn_type_verify.http.req 2", 'Should have a http request.')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
