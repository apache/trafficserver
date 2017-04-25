'''
Test the VIA header. This runs several requests through ATS and extracts the upstream VIA headers.
Those are then checked against a gold file to verify the protocol stack based output is correct.
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
Check VIA header for protocol stack data.
'''

# Check if the local curl has a specific feature.
# This should be made generally available.
def RequireCurlFeature(tag):
  FEATURE_TAG = 'Features:'
  tag = tag.lower()
  try:
    text = subprocess.check_output(['curl', '--version'], universal_newlines = True)
    for line in text.splitlines():
      if (line.startswith(FEATURE_TAG)):
        line = line[len(FEATURE_TAG):].lower()
        tokens = line.split()
        for t in tokens:
          if t == tag:
            return True
  except subprocess.CalledProcessError:
    pass # no curl at all, it clearly doesn't have the required feature.
  return False

Test.SkipUnless(
    Condition.Condition(lambda : RequireCurlFeature('http2'), "Via test requires a curl that supports HTTP/2")
    )
Test.ContinueOnFail=True

# Define default ATS
ts=Test.MakeATSProcess("ts",select_ports=False)
server=Test.MakeOriginServer("server", options={'--load' : os.path.join(Test.TestDirectory, 'via-observer.py')})

testName = "VIA"

# We only need one transaction as only the VIA header will be checked.
request_header={"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header={"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# These should be promoted rather than other tests like this reaching around.
ts.addSSLfile("../remap/ssl/server.pem")
ts.addSSLfile("../remap/ssl/server.key")

ts.Variables.ssl_port = 4443
ts.Disk.records_config.update({
        'proxy.config.http.insert_request_via_str' : 1,
        'proxy.config.http.insert_response_via_str' : 1,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.http.server_ports': '{0} {1}:proto=http2;http:ssl'.format(ts.Variables.port,ts.Variables.ssl_port),
    })

ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    'map https://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port,ts.Variables.ssl_port)
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Set up to check the output after the tests have run.
via_log_id = Test.Disk.File("via.log")
via_log_id.Content = "via.gold"

# Ask the OS if the port is ready for connect()
def CheckPort(Port) :
    return lambda : 0 == subprocess.call('netstat --listen --tcp -n | grep -q :{}'.format(Port), shell=True)

# Basic HTTP 1.1
tr=Test.AddTestRun()
# Wait for the micro server
tr.Processes.Default.StartBefore(server, ready=CheckPort(server.Variables.Port))
# Delay on readiness of our ssl ports
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=CheckPort(ts.Variables.ssl_port))

tr.Processes.Default.Command='curl --verbose --http1.1 --proxy localhost:{} http://www.example.com'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode=0

tr.StillRunningAfter=server
tr.StillRunningAfter=ts

# HTTP 1.0
tr=Test.AddTestRun()
tr.Processes.Default.Command='curl --verbose --http1.0 --proxy localhost:{} http://www.example.com'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode=0

tr.StillRunningAfter=server
tr.StillRunningAfter=ts

# HTTP 2
tr=Test.AddTestRun()
tr.Processes.Default.Command='curl --verbose --insecure --header "Host: www.example.com" https://localhost:{}'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode=0

tr.StillRunningAfter=server
tr.StillRunningAfter=ts

# TLS
tr=Test.AddTestRun()
tr.Processes.Default.Command='curl --verbose --http1.1 --insecure --header "Host: www.example.com" https://localhost:{}'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode=0

tr.StillRunningAfter=server
tr.StillRunningAfter=ts
