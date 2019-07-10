'''
Test normalizations of the Accept-Encoding header field.
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
Test normalizations of the Accept-Encoding header field.
'''

Test.SkipUnless(
    Condition.HasATSFeature('TS_HAS_BROTLI')
)

Test.ContinueOnFail = True

server = Test.MakeOriginServer("server", options={'--load': os.path.join(Test.TestDirectory, 'normalize_ae_observer.py')})

testName = "NORMALIZE_AE"

request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.no-oride.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.ae-0.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.ae-1.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.ae-2.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# Define first ATS
ts = Test.MakeATSProcess("ts", select_ports=True)


def baselineTsSetup(ts):

    ts.Disk.records_config.update({
        # 'proxy.config.diags.debug.enabled': 1,
        'proxy.config.http.cache.http': 0,  # Make sure each request is sent to the origin server.
    })

    ts.Disk.remap_config.AddLine(
        'map http://www.no-oride.com http://127.0.0.1:{0}'.format(server.Variables.Port)
    )
    ts.Disk.remap_config.AddLine(
        'map http://www.ae-0.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
        ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=0'
    )
    ts.Disk.remap_config.AddLine(
        'map http://www.ae-1.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
        ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=1'
    )
    ts.Disk.remap_config.AddLine(
        'map http://www.ae-2.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
        ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=2'
    )


baselineTsSetup(ts)

# set up to check the output after the tests have run.
#
normalize_ae_log_id = Test.Disk.File("normalize_ae.log")
normalize_ae_log_id.Content = "normalize_ae.gold"

# Try various Accept-Encoding header fields for a particular traffic server and host.


def allAEHdrs(shouldWaitForUServer, shouldWaitForTs, ts, host):

    tr = test.AddTestRun()

    if shouldWaitForUServer:
        # wait for the micro server
        tr.Processes.Default.StartBefore(server)

    if shouldWaitForTs:
        # wait for the micro server
        # delay on readiness of port
        tr.Processes.Default.StartBefore(ts)

    baseCurl = 'curl --verbose --ipv4 --http1.1 --proxy localhost:{} '.format(ts.Variables.port)

    # No Accept-Encoding header.
    #
    tr.Processes.Default.Command = baseCurl + '--header "X-Au-Test: {0}" http://{0}'.format(host)
    tr.Processes.Default.ReturnCode = 0

    def curlTail(hdrValue):
        return '--header "Accept-Encoding: {}" http://'.format(hdrValue) + host

    tr = test.AddTestRun()
    tr.Processes.Default.Command = baseCurl + curlTail('gzip')
    tr.Processes.Default.ReturnCode = 0

    tr = test.AddTestRun()
    tr.Processes.Default.Command = baseCurl + curlTail('x-gzip')
    tr.Processes.Default.ReturnCode = 0

    tr = test.AddTestRun()
    tr.Processes.Default.Command = baseCurl + curlTail('br')
    tr.Processes.Default.ReturnCode = 0

    tr = test.AddTestRun()
    tr.Processes.Default.Command = baseCurl + curlTail('gzip, br')
    tr.Processes.Default.ReturnCode = 0

    tr = test.AddTestRun()
    tr.Processes.Default.Command = baseCurl + curlTail('gzip;q=0.3, whatever;q=0.666, br;q=0.7')
    tr.Processes.Default.ReturnCode = 0


def perTsTest(shouldWaitForUServer, ts):
    allAEHdrs(shouldWaitForUServer, True, ts, 'www.no-oride.com')
    allAEHdrs(False, False, ts, 'www.ae-0.com')
    allAEHdrs(False, False, ts, 'www.ae-1.com')
    allAEHdrs(False, False, ts, 'www.ae-2.com')


perTsTest(True, ts)

# Define second ATS
ts2 = Test.MakeATSProcess("ts2", select_ports=True)

baselineTsSetup(ts2)

ts2.Disk.records_config.update({
    'proxy.config.http.normalize_ae': 0,
})

perTsTest(False, ts2)
