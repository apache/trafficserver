'''
Verify that tunnel active timeout produces ERR_TUN_ACTIVE_TIMEOUT squid code.
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
Verify that tunnel active timeout produces ERR_TUN_ACTIVE_TIMEOUT squid code.
'''

ts = Test.MakeATSProcess("ts", enable_tls=True)
server = Test.MakeOriginServer("server", ssl=True)

# Simple response from origin
request_header = {"headers": "GET / HTTP/1.1\r\nHost: server\r\n\r\n", "timestamp": "1234", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1234", "body": "hello"}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addDefaultSSLFiles()

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|ssl|tunnel',
        'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.http.connect_ports': f'{server.Variables.SSL_Port}',
        # Set a short active timeout for tunnels (2 seconds)
        'proxy.config.http.transaction_active_timeout_in': 2,
        # Force log flush every second for test reliability
        'proxy.config.log.max_secs_per_buffer': 1,
    })

ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server.Variables.SSL_Port}')

# Configure custom log format to capture squid code
ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: custom
      format: '%<crc> %<pssc> %<cqhm>'
  logs:
    - filename: squid.log
      format: custom
'''.split("\n"))

# Test: Perform a CONNECT request that will time out
tr = Test.AddTestRun("Tunnel active timeout test")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)

# Use the tunnel_timeout_client.py script to establish a CONNECT tunnel and then
# just hold the connection until ATS times it out
tr.Setup.Copy('tunnel_timeout_client.py')

# Connect, establish tunnel, then sleep to trigger active timeout
tr.Processes.Default.Command = (
    f'{sys.executable} tunnel_timeout_client.py 127.0.0.1 {ts.Variables.port} '
    f'127.0.0.1 {server.Variables.SSL_Port} 5')
# The connection will be closed by ATS due to timeout
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# Wait for the access log to be written
tr = Test.AddTestRun("Wait for the access log to write out")
tr.DelayStart = 3
tr.StillRunningAfter = ts
tr.Processes.Default.Command = 'echo "waiting for log flush"'
tr.Processes.Default.ReturnCode = 0

# Verify the squid code in the access log
ts.Disk.File(os.path.join(ts.Variables.LOGDIR, 'squid.log')).Content = Testers.ContainsExpression(
    'ERR_TUN_ACTIVE_TIMEOUT.*CONNECT', 'Verify the tunnel timeout squid code is logged')
