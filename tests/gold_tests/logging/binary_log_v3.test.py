'''
End-to-end test of the self-describing v3 binary log format.
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
Write a v3 binary log and read it back with traffic_logcat (ASCII + JSON),
and prove the per-LogObject binary_log_version:2 writer is still readable.
'''

# chi resolves to the client address; a Unix-domain-socket curl changes it.
Test.SkipIf(Condition.CurlUsingUnixDomainSocket())


class BinaryLogV3Test:
    ''' Drive traffic into a v3 binary log, then decode it with traffic_logcat
    and compare the ASCII and JSON output against gold files.
    '''

    # A counter to give each ATS process a unique name.
    __ts_counter = 1

    # Number of transactions (and therefore log entries) to generate.
    num_requests = 3

    # One format exercising all four wire types:
    #   chi  = client IP         -> IP     -> "127.0.0.1"
    #   cqu  = request URL       -> STRING -> "http://127.0.0.1:<port>/get"
    #   pssc = response status   -> sINT   -> "200"
    #   sshv = resp HTTP version -> dINT   -> "HTTP/1.1" (ASCII) / [1,1] (JSON)
    # The dynamic listen port in cqu is masked with `` in the gold files.
    log_format = '%<chi> %<cqu> %<pssc> %<sshv>'

    def __init__(self):
        # Origin returning a plain 200 over HTTP/1.1 (so sshv == HTTP/1.1).
        self.httpbin = Test.MakeHttpBinServer("httpbin")
        self.ts = self.__configure_traffic_server()
        self.__configure_traffic_test_run()
        self.__configure_decode_test_runs()

    def __configure_traffic_server(self):
        ''' Configure ATS with the ASCII, v2, and v3 binary log objects.

        Return:
            The traffic_server process.
        '''
        name = f"ts{BinaryLogV3Test.__ts_counter}"
        BinaryLogV3Test.__ts_counter += 1
        ts = Test.MakeATSProcess(name)

        ts.Disk.records_config.update(
            {
                # No caching: every request reaches the origin, so sshv is populated.
                'proxy.config.http.cache.http': 0,
                # Flush quickly so the .blog is on disk shortly after the requests.
                'proxy.config.log.max_secs_per_buffer': 1,
                'proxy.config.log.periodic_tasks_interval': 1,
            })

        ts.Disk.remap_config.AddLine(f'map http://127.0.0.1:{ts.Variables.port}/ http://127.0.0.1:{self.httpbin.Variables.Port}/')

        ts.Disk.logging_yaml.AddLines(
            f'''
            logging:
              formats:
                - name: custom_fmt
                  format: "{self.log_format}"
              logs:
                - filename: v2
                  format: custom_fmt
                  mode: binary
                  binary_log_version: 2
                - filename: v3
                  format: custom_fmt
                  mode: binary
                  binary_log_version: 3
                - filename: ascii
                  format: custom_fmt
                  mode: ascii
            '''.split("\n"))

        logdir = ts.Variables.LOGDIR
        self.v2_blog = os.path.join(logdir, 'v2.blog')
        self.v3_blog = os.path.join(logdir, 'v3.blog')
        self.ascii_log = os.path.join(logdir, 'ascii.log')
        return ts

    def __configure_traffic_test_run(self):
        ''' Generate num_requests identical origin-backed transactions. '''
        url = f'http://127.0.0.1:{self.ts.Variables.port}/get'
        for i in range(BinaryLogV3Test.num_requests):
            tr = Test.AddTestRun(f'Generate binary log traffic #{i + 1}')
            if i == 0:
                tr.Processes.Default.StartBefore(self.httpbin)
                tr.Processes.Default.StartBefore(self.ts)
            tr.MakeCurlCommand(f'--http1.1 "{url}"', ts=self.ts)
            tr.Processes.Default.ReturnCode = 0

    def __configure_decode_test_runs(self):
        ''' Decode the binary logs with traffic_logcat and compare to gold. '''
        logcat = os.path.join(self.ts.Variables.BINDIR, 'traffic_logcat')

        # Wait until the ASCII companion has every entry; the binary logs are
        # written in the same periodic flush, so they are complete by then.
        Test.AddAwaitFileContainsTestRun('Await the logs to flush.', self.ascii_log, r'/get', BinaryLogV3Test.num_requests)

        # The binary_log_version:2 log decodes to the same ASCII as v3.
        tr = Test.AddTestRun('Decode the v2 binary log to ASCII')
        tr.Processes.Default.Command = f'{logcat} {self.v2_blog}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = 'gold/binary_log_v3_ascii.gold'

        # traffic_logcat decodes the v3 .blog to the expected ASCII.
        tr = Test.AddTestRun('Decode the v3 binary log to ASCII')
        tr.Processes.Default.Command = f'{logcat} {self.v3_blog}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = 'gold/binary_log_v3_ascii.gold'

        # traffic_logcat -j decodes the v3 .blog to the expected JSON.
        tr = Test.AddTestRun('Decode the v3 binary log to JSON')
        tr.Processes.Default.Command = f'{logcat} -j {self.v3_blog}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = 'gold/binary_log_v3_json.gold'

        # traffic_logcat -H prints the v3 segment header, including the
        # self-describing field-type schema, instead of decoding entries.
        tr = Test.AddTestRun('Print the v3 binary log header')
        tr.Processes.Default.Command = f'{logcat} -H {self.v3_blog}'
        tr.Processes.Default.ReturnCode = 0
        stdout = tr.Processes.Default.Streams.stdout
        stdout += Testers.ContainsExpression(r'version:\s+3', 'The header reports segment version 3.')
        stdout += Testers.ContainsExpression(r'format_type:\s+4 \(CUSTOM\)', 'The header reports the custom format type.')
        stdout += Testers.ContainsExpression(r'fieldlist:\s+chi,cqu,pssc,sshv', 'The header lists the format field symbols.')
        stdout += Testers.ContainsExpression(r'field_type_schema:\s+field_count=4', 'The v3 header has a 4-field type schema.')
        stdout += Testers.ContainsExpression(r'chi\s+IP', 'chi is framed as an IP.')
        stdout += Testers.ContainsExpression(r'cqu\s+STRING', 'cqu is framed as a STRING.')
        stdout += Testers.ContainsExpression(r'pssc\s+sINT', 'pssc is framed as an sINT.')
        stdout += Testers.ContainsExpression(r'sshv\s+dINT', 'sshv is framed as a dINT.')

        # traffic_logcat -H also prints the v2 header, but v2 segments carry no
        # field-type schema.
        tr = Test.AddTestRun('Print the v2 binary log header')
        tr.Processes.Default.Command = f'{logcat} -H {self.v2_blog}'
        tr.Processes.Default.ReturnCode = 0
        stdout = tr.Processes.Default.Streams.stdout
        stdout += Testers.ContainsExpression(r'version:\s+2', 'The header reports segment version 2.')
        stdout += Testers.ContainsExpression(r'fieldlist:\s+chi,cqu,pssc,sshv', 'The header lists the format field symbols.')
        stdout += Testers.ExcludesExpression(r'field_type_schema', 'A v2 segment has no field-type schema.')


#
# Run the test.
#
BinaryLogV3Test()
