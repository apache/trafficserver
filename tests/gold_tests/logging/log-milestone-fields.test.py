'''
Verify that msdms milestone difference fields produce valid output for both
cache-miss and cache-hit transactions.  This exercises the Phase 1 timing
fields proposed for the squid.log local_disk format.
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

Test.Summary = 'Verify msdms milestone logging fields for cache miss and cache hit paths'
Test.ContinueOnFail = True


class MilestoneFieldsTest:
    """
    Sends two requests for the same cacheable URL: the first is a cache miss
    that populates the cache, the second is a cache hit served from RAM cache.
    A custom log format records all Phase 1 milestone timing fields plus the
    cache result code.  A validation script then parses the log and checks:

    - Every expected key=value pair is present on each line
    - All values are integers (>= 0 or -1 for unset milestones)
    - Cache miss line: ms > 0, origin-phase fields present
    - Cache hit line: hit_proc >= 0 and hit_xfer >= 0
    - No epoch-length garbage values (> 1_000_000_000)
    """

    # All Phase 1 msdms fields plus ms and cache result code for identification.
    LOG_FORMAT = (
        'crc=%<crc> ms=%<ttms>'
        ' c_ttfb=%<{TS_MILESTONE_UA_BEGIN_WRITE-TS_MILESTONE_SM_START}msdms>'
        ' c_tls=%<{TS_MILESTONE_TLS_HANDSHAKE_END-TS_MILESTONE_TLS_HANDSHAKE_START}msdms>'
        ' c_hdr=%<{TS_MILESTONE_UA_READ_HEADER_DONE-TS_MILESTONE_SM_START}msdms>'
        ' c_proc=%<{TS_MILESTONE_CACHE_OPEN_READ_BEGIN-TS_MILESTONE_UA_READ_HEADER_DONE}msdms>'
        ' cache=%<{TS_MILESTONE_CACHE_OPEN_READ_END-TS_MILESTONE_CACHE_OPEN_READ_BEGIN}msdms>'
        ' dns=%<{TS_MILESTONE_SERVER_FIRST_CONNECT-TS_MILESTONE_CACHE_OPEN_READ_END}msdms>'
        ' o_tcp=%<{TS_MILESTONE_SERVER_CONNECT_END-TS_MILESTONE_SERVER_FIRST_CONNECT}msdms>'
        ' o_wait=%<{TS_MILESTONE_SERVER_FIRST_READ-TS_MILESTONE_SERVER_CONNECT_END}msdms>'
        ' o_hdr=%<{TS_MILESTONE_SERVER_READ_HEADER_DONE-TS_MILESTONE_SERVER_FIRST_READ}msdms>'
        ' o_proc=%<{TS_MILESTONE_UA_BEGIN_WRITE-TS_MILESTONE_SERVER_READ_HEADER_DONE}msdms>'
        ' o_body=%<{TS_MILESTONE_SERVER_CLOSE-TS_MILESTONE_UA_BEGIN_WRITE}msdms>'
        ' c_xfer=%<{TS_MILESTONE_SM_FINISH-TS_MILESTONE_SERVER_CLOSE}msdms>'
        ' hit_proc=%<{TS_MILESTONE_UA_BEGIN_WRITE-TS_MILESTONE_CACHE_OPEN_READ_END}msdms>'
        ' hit_xfer=%<{TS_MILESTONE_SM_FINISH-TS_MILESTONE_UA_BEGIN_WRITE}msdms>')

    def __init__(self):
        self._server = Test.MakeOriginServer("server")
        self._nameserver = Test.MakeDNServer("dns", default='127.0.0.1')
        self._setupOriginServer()
        self._setupTS()

    def _setupOriginServer(self):
        self._server.addResponse(
            "sessionlog.json",
            {
                'timestamp': 100,
                "headers": "GET /cacheable HTTP/1.1\r\nHost: example.com\r\n\r\n",
                "body": "",
            },
            {
                'timestamp': 100,
                "headers":
                    (
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Cache-Control: max-age=300\r\n"
                        "Connection: close\r\n"
                        "\r\n"),
                "body": "This is a cacheable response body for milestone testing.",
            },
        )

    def _setupTS(self):
        self._ts = Test.MakeATSProcess("ts", enable_cache=True)

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|log',
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._nameserver.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.http.cache.http': 1,
                'proxy.config.log.max_secs_per_buffer': 1,
            })

        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}/')

        self._ts.Disk.logging_yaml.AddLines(
            f'''
logging:
  formats:
    - name: milestone_test
      format: '{self.LOG_FORMAT}'
  logs:
    - filename: milestone_fields
      format: milestone_test
      mode: ascii
'''.split("\n"))

    @property
    def _log_path(self) -> str:
        return os.path.join(self._ts.Variables.LOGDIR, 'milestone_fields.log')

    @property
    def _validate_script(self) -> str:
        return os.path.join(Test.TestDirectory, 'verify_milestone_fields.py')

    def run(self):
        self._sendCacheMiss()
        self._waitForCacheIO()
        self._sendCacheHit()
        self._waitForLog()
        self._validateLog()

    def _sendCacheMiss(self):
        tr = Test.AddTestRun('Cache miss request')
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._nameserver)
        tr.Processes.Default.StartBefore(self._ts)
        tr.MakeCurlCommand(
            f'--verbose --header "Host: example.com" '
            f'http://127.0.0.1:{self._ts.Variables.port}/cacheable', ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

    def _waitForCacheIO(self):
        tr = Test.AddTestRun('Wait for cache write to complete')
        tr.Processes.Default.Command = 'sleep 2'
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

    def _sendCacheHit(self):
        tr = Test.AddTestRun('Cache hit request')
        tr.MakeCurlCommand(
            f'--verbose --header "Host: example.com" '
            f'http://127.0.0.1:{self._ts.Variables.port}/cacheable', ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

    def _waitForLog(self):
        tr = Test.AddTestRun('Wait for log file to be written')
        tr.Processes.Default.Command = (os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + f' 60 1 -f {self._log_path}')
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

    def _validateLog(self):
        tr = Test.AddTestRun('Validate milestone fields in log')
        tr.Processes.Default.Command = f'python3 {self._validate_script} {self._log_path}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.TimeOut = 10
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('PASS', 'Validation script should report PASS')
        tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression('FAIL', 'Validation script should not report FAIL')


MilestoneFieldsTest().run()
