'''
Test that Trafficserver rejects requests for host 0
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
Test that Trafficserver rejects requests for host 0
'''

Test.ContinueOnFail = True

HOST1 = 'redirect.test'

redirect_serv = Test.MakeOriginServer("redirect_serv", ip='0.0.0.0')

dns = Test.MakeDNServer("dns")
dns.addRecords(records={HOST1: ['127.0.0.1']})

ts = Test.MakeATSProcess("ts", enable_cache=False)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|dns|redirect',
    'proxy.config.http.redirection_enabled': 1,
    'proxy.config.http.number_of_redirections': 1,
    'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
    'proxy.config.dns.resolv_conf': 'NULL',
    'proxy.config.url_remap.remap_required': 0  # need this so the domain gets a chance to be evaluated through DNS
})

Test.Setup.Copy(os.path.join(Test.Variables.AtsTestToolsDir, 'tcp_client.py'))

data_dirname = 'generated_test_data'
data_path = os.path.join(Test.TestDirectory, data_dirname)
os.makedirs(data_path, exist_ok=True)
gold_filepath = os.path.join(data_path, 'deny0.gold')
with open(os.path.join(data_path, 'deny0.gold'), 'w') as f:
    f.write('HTTP/1.1 400 Bad Destination Address\r\n')

isFirstTest = True


def buildMetaTest(testName, requestString):
    tr = Test.AddTestRun(testName)
    global isFirstTest
    if isFirstTest:
        isFirstTest = False
        tr.Processes.Default.StartBefore(ts)
        tr.Processes.Default.StartBefore(redirect_serv, ready=When.PortOpen(redirect_serv.Variables.Port))
        tr.Processes.Default.StartBefore(dns)
    with open(os.path.join(data_path, tr.Name), 'w') as f:
        f.write(requestString)
    tr.Processes.Default.Command = "python3 tcp_client.py 127.0.0.1 {0} {1} | head -1".format(
        ts.Variables.port, os.path.join(data_dirname, tr.Name))
    tr.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = gold_filepath
    tr.StillRunningAfter = ts
    tr.StillRunningAfter = redirect_serv
    tr.StillRunningAfter = dns


buildMetaTest('RejectInterfaceAnyIpv4',
              'GET / HTTP/1.1\r\nHost: 0:{port}\r\nConnection: close\r\n\r\n'.format(port=ts.Variables.port))


buildMetaTest('RejectInterfaceAnyIpv6',
              'GET / HTTP/1.1\r\nHost: [::]:{port}\r\nConnection: close\r\n\r\n'.format(port=ts.Variables.portv6))


# Sets up redirect to IPv4 ANY address
redirect_request_header = {"headers": "GET /redirect-0 HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
redirect_response_header = {"headers": "HTTP/1.1 302 Found\r\nLocation: http://0:{0}/\r\nConnection: close\r\n\r\n".format(
    ts.Variables.port), "timestamp": "5678", "body": ""}
redirect_serv.addResponse("sessionfile.log", redirect_request_header, redirect_response_header)

buildMetaTest('RejectRedirectToInterfaceAnyIpv4',
              'GET /redirect-0 HTTP/1.1\r\nHost: {host}:{port}\r\n\r\n'.format(host=HOST1, port=redirect_serv.Variables.Port))


# Sets up redirect to IPv6 ANY address
redirect_request_header = {"headers": "GET /redirect-0v6 HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
redirect_response_header = {"headers": "HTTP/1.1 302 Found\r\nLocation: http://[::]:{0}/\r\nConnection: close\r\n\r\n".format(
    ts.Variables.port), "timestamp": "5678", "body": ""}
redirect_serv.addResponse("sessionfile.log", redirect_request_header, redirect_response_header)

buildMetaTest('RejectRedirectToInterfaceAnyIpv6',
              'GET /redirect-0v6 HTTP/1.1\r\nHost: {host}:{port}\r\n\r\n'.format(host=HOST1, port=redirect_serv.Variables.Port))


Test.Setup.Copy(data_path)
