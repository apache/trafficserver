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
import sys
from ports import get_port
import re

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
request_tunnel_header = {"headers": "GET / HTTP/1.1\r\nHost: tunnel-test\r\n\r\n",
                         "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_tunnel_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length:0\r\n\r\n",
                          "timestamp": "1469733493.993", "body": ""}


Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir,
                                    'tunnel_transform.so'), ts)

# add response to the server dictionary
server.addResponse("sessionfile.log", request_tunnel_header, response_tunnel_header)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'http|test',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
    'proxy.config.http.connect_ports': '{0}'.format(server.Variables.SSL_Port)
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: tunnel-test',
    "  tunnel_route: localhost:{0}".format(server.Variables.SSL_Port),
])

# Set up simple forwarding proxy to keep track of TLS bytes for both
# directions
tr = Test.AddTestRun("Run dumb proxy and send tunnel request.")
tr.Setup.CopyAs('dumb_proxy.py', tr.RunDirectory)
dumb_proxy = tr.Processes.Process(
    f'dumb-proxy')
proxy_port = get_port(dumb_proxy, "listening_port")
dumb_proxy.Command = f'{sys.executable} dumb_proxy.py --listening_port {proxy_port} --forwarding_port {ts.Variables.ssl_port}'
dumb_proxy.StartBefore(Test.Processes.ts)
dumb_proxy.Ready = When.PortOpenv4(proxy_port)
dumb_proxy.ReturnCode = 0
# Record the log file path for later verification.
proxy_output = dumb_proxy.Streams.stdout.AbsPath

# Add connection close to ensure that the client connection closes promptly after completing the transaction
cmd_tunnel = 'curl -k --http1.1 -H "Connection: close" -vs --resolve "tunnel-test:{0}:127.0.0.1"  https://tunnel-test:{0}/'.format(
    proxy_port)

# Send the tunnel request
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = cmd_tunnel
tr.Processes.Default.ReturnCode = 0
tr.TimeOut = 5
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.SSL_Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(dumb_proxy)
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
            "traffic_ctl metric get tunnel_transform.test.done",
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
tr.Processes.Default.Command = 'traffic_ctl metric get tunnel_transform.error'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'tunnel_transform.error 0', 'incorrect statistic return, or possible error.')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server


def get_expected_bytes(path_to_proxy_output, key):
    # Construct the regex pattern.
    pattern = re.compile(rf'{re.escape(key)}:\s+(\d+)')
    with open(path_to_proxy_output, 'r') as file:
        log_content = file.read()
    bytes_transferred = 0
    match = pattern.search(log_content)
    if match:
        bytes_transferred = int(match.group(1))
    return bytes_transferred


def check_byte_count(plugin_metric_path, proxy_output_path, key):
    expected_bytes = get_expected_bytes(proxy_output_path, key)
    f = open(plugin_metric_path, 'r')
    content = f.read()
    values = content.split()
    f.close()
    if len(values) == 2:
        val = int(values[1])
        return val == expected_bytes, "Check byte count", "Byte count does not match the expected value"
    else:
        return False, "Check byte count", "Unexpected metrics output format"


tr = Test.AddTestRun("Fetch bytes sent")
tr.Processes.Default.Command = "traffic_ctl metric get tunnel_transform.ua.bytes_sent"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

path1 = tr.Processes.Default.Streams.stdout.AbsPath

tr2 = Test.AddTestRun("Check the input bytes sent and fetch outptut bytes sent")
tr2.Processes.Default.Command = 'traffic_ctl metric get tunnel_transform.os.bytes_sent'
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Env = ts.Env
tr2.StillRunningAfter = ts
tr2.StillRunningAfter = server
tr2.Processes.Default.Streams.stdout = Testers.Lambda(
    lambda info, tester: check_byte_count(
        path1, proxy_output, 'client-to-server'))

path2 = tr2.Processes.Default.Streams.stdout.AbsPath

tr = Test.AddTestRun("Check that output bytes sent")
tr.Processes.Default.Command = 'echo foo'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Streams.stdout = Testers.Lambda(lambda info, tester: check_byte_count(path2, proxy_output, 'server-to-client'))
