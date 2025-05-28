'''
Test ZSTD normalization modes 4 and 5 for the Accept-Encoding header field.
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
Test ZSTD normalization modes 4 and 5 for the Accept-Encoding header field.
'''

Test.ContinueOnFail = True

server = Test.MakeOriginServer("server", options={'--load': os.path.join(Test.TestDirectory, 'normalize_ae_observer.py')})

testName = "NORMALIZE_AE_ZSTD"

# Add server responses for ZSTD test hosts
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.ae-4.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.ae-5.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# Define ATS with ZSTD normalization modes
ts = Test.MakeATSProcess("ts", enable_cache=False)

ts.Disk.records_config.update({
    # 'proxy.config.diags.debug.enabled': 1,
})

# Mode 4: ZSTD priority with fallback to br, gzip
ts.Disk.remap_config.AddLine(
    'map http://www.ae-4.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=4')

# Mode 5: ZSTD combination mode supporting all permutations
ts.Disk.remap_config.AddLine(
    'map http://www.ae-5.com http://127.0.0.1:{0}'.format(server.Variables.Port) +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=5')

# Set up to check the output
normalize_ae_log_id = Test.Disk.File("normalize_ae.log")
normalize_ae_log_id.Content = "normalize_ae_zstd.gold"


def testZstdNormalization(host, mode_description):
    """Test various ZSTD Accept-Encoding combinations for a specific host"""

    test = Test
    baseCurl = '--verbose --ipv4 --http1.1 --proxy localhost:{} '.format(ts.Variables.port)

    print(f"Testing {mode_description} - {host}")

    # Test 1: No Accept-Encoding header
    tr = test.AddTestRun()
    tr.Processes.Default.StartBefore(server)
    tr.Processes.Default.StartBefore(ts)
    tr.MakeCurlCommand(baseCurl + '--header "X-Au-Test: {0}" http://{0}'.format(host))
    tr.Processes.Default.ReturnCode = 0

    def curlTail(hdrValue):
        return '--header "Accept-Encoding: {}" http://'.format(hdrValue) + host

    # Test 2: Pure ZSTD
    tr = test.AddTestRun()
    tr.MakeCurlCommand(baseCurl + curlTail('zstd'))
    tr.Processes.Default.ReturnCode = 0

    # Test 3: ZSTD with gzip
    tr = test.AddTestRun()
    tr.MakeCurlCommand(baseCurl + curlTail('zstd, gzip'))
    tr.Processes.Default.ReturnCode = 0

    # Test 4: ZSTD with brotli
    tr = test.AddTestRun()
    tr.MakeCurlCommand(baseCurl + curlTail('zstd, br'))
    tr.Processes.Default.ReturnCode = 0

    # Test 5: All three algorithms
    tr = test.AddTestRun()
    tr.MakeCurlCommand(baseCurl + curlTail('zstd, br, gzip'))
    tr.Processes.Default.ReturnCode = 0

    # Test 6: Different order (should normalize to same result)
    tr = test.AddTestRun()
    tr.MakeCurlCommand(baseCurl + curlTail('gzip, zstd, br'))
    tr.Processes.Default.ReturnCode = 0

    # Test 7: ZSTD with quality values
    tr = test.AddTestRun()
    tr.MakeCurlCommand(baseCurl + curlTail('zstd;q=0.9, br;q=0.8, gzip;q=0.7'))
    tr.Processes.Default.ReturnCode = 0

    # Test 8: ZSTD with unsupported algorithms (should filter to zstd)
    tr = test.AddTestRun()
    tr.MakeCurlCommand(baseCurl + curlTail('deflate, zstd, compress'))
    tr.Processes.Default.ReturnCode = 0

    # Test 9: Only unsupported algorithms with no ZSTD (should fall back or remove)
    tr = test.AddTestRun()
    tr.MakeCurlCommand(baseCurl + curlTail('deflate, compress, identity'))
    tr.Processes.Default.ReturnCode = 0

    # Test 10: ZSTD with brotli and gzip in different order
    tr = test.AddTestRun()
    tr.MakeCurlCommand(baseCurl + curlTail('br, zstd, gzip'))
    tr.Processes.Default.ReturnCode = 0

    # Test 11: Legacy algorithms without ZSTD (fallback behavior)
    tr = test.AddTestRun()
    tr.MakeCurlCommand(baseCurl + curlTail('gzip, br'))
    tr.Processes.Default.ReturnCode = 0

    # Test 12: Only gzip (fallback test)
    tr = test.AddTestRun()
    tr.MakeCurlCommand(baseCurl + curlTail('gzip'))
    tr.Processes.Default.ReturnCode = 0


# Run tests for mode 4 (ZSTD priority)
testZstdNormalization('www.ae-4.com', 'Mode 4: ZSTD Priority')

# Run tests for mode 5 (ZSTD combinations)
testZstdNormalization('www.ae-5.com', 'Mode 5: ZSTD Combinations')
