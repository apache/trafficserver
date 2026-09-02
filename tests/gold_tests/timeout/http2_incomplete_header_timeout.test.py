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

import sys
from typing import Optional

Test.Summary = 'Verify http2.incomplete_header_timeout_in'

# VC_EVENT_ACTIVE_TIMEOUT. The incomplete header timeout is an active timeout on
# the stream, so ATS reports this event rather than VC_EVENT_INACTIVITY_TIMEOUT
# (105).
VC_EVENT_ACTIVE_TIMEOUT = 106

# Http2ErrorCode::HTTP2_ERROR_COMPRESSION_ERROR. ATS tears down the connection
# rather than just the stream because the undelivered CONTINUATION frame leaves
# its HPACK dynamic table out of sync.
HTTP2_ERROR_COMPRESSION_ERROR = 9


class TestHttp2IncompleteHeaderTimeout:
    """Configure a test for http2.incomplete_header_timeout_in.

    The client sends a HEADERS frame without the END_HEADERS flag, which leaves
    ATS waiting for a CONTINUATION frame. incomplete_header_timeout_in bounds
    how long ATS waits.
    """

    client_script: str = 'http2_incomplete_header_client.py'
    replay_file: str = 'replay/http2_incomplete_header_timeout.replay.yaml'
    counter: int = 0

    def __init__(
            self,
            name: str,
            incomplete_header_timeout_in: int,
            path: str,
            uuid: str,
            end_headers: bool = False,
            continuation_delay: Optional[float] = None,
            min_elapsed: Optional[float] = None,
            max_elapsed: Optional[float] = None,
            expect_timeout: bool = False):
        """Initialize the test.

        :param name: The name of the test run.
        :param incomplete_header_timeout_in: The value to configure for
          proxy.config.http2.incomplete_header_timeout_in.
        :param path: The :path pseudo header the client requests.
        :param uuid: The uuid of the replay file transaction to request.
        :param end_headers: Whether the client sends the complete header block
          with END_HEADERS in a single HEADERS frame.
        :param continuation_delay: How long after the HEADERS frame the client
          waits before sending the END_HEADERS CONTINUATION frame. None means
          that the client never sends it.
        :param min_elapsed: Fail if ATS ends the stream sooner than this.
        :param max_elapsed: Fail if ATS ends the stream later than this.
        :param expect_timeout: Whether ATS is expected to time out the stream.
        """
        self._name = name
        self._incomplete_header_timeout_in = incomplete_header_timeout_in
        self._path = path
        self._uuid = uuid
        self._end_headers = end_headers
        self._continuation_delay = continuation_delay
        self._min_elapsed = min_elapsed
        self._max_elapsed = max_elapsed
        self._expect_timeout = expect_timeout

    def _configure_server(self, tr: 'TestRun') -> None:
        """Configure the origin server.

        :param tr: The TestRun object to associate the server process with.
        """
        self._server = tr.AddVerifierServerProcess(f'server-{self.counter}', self.replay_file)

    def _configure_traffic_server(self, tr: 'TestRun') -> None:
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        ts = tr.MakeATSProcess(f'ts-{self.counter}', enable_tls=True, enable_cache=False)
        self._ts = ts

        ts.addSSLfile('ssl/cert.crt')
        ts.addSSLfile('ssl/private-key.key')
        ts.Disk.ssl_multicert_yaml.AddLines(
            f"""
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: {ts.Variables.SSLDir}/cert.crt
    ssl_key_name: {ts.Variables.SSLDir}/private-key.key
""".split('\n'))

        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http2',
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.http2.incomplete_header_timeout_in': self._incomplete_header_timeout_in,
            })

        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')

    def _configure_client(self, tr: 'TestRun') -> None:
        """Configure the ad hoc HTTP/2 client.

        :param tr: The TestRun object to associate the client process with.
        """
        tr.Setup.Copy(self.client_script)
        command = (
            f'{sys.executable} {self.client_script} {self._ts.Variables.ssl_port} '
            f'--path {self._path} --uuid {self._uuid}')
        if self._end_headers:
            command += ' --end-headers'
        if self._continuation_delay is not None:
            command += f' --continuation-delay {self._continuation_delay}'
        if self._min_elapsed is not None:
            command += f' --min-elapsed {self._min_elapsed}'
        if self._max_elapsed is not None:
            command += f' --max-elapsed {self._max_elapsed}'

        tr.Processes.Default.Command = command
        tr.Processes.Default.ReturnCode = 0

    def _configure_expectations(self, tr: 'TestRun') -> None:
        """Configure the testers for the test run.

        :param tr: The TestRun object to associate the testers with.
        """
        timeout_error = 'ERROR: HTTP/2 stream error timeout'
        if self._expect_timeout:
            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                f'GOAWAY error_code={HTTP2_ERROR_COMPRESSION_ERROR} last_stream_id=1',
                'ATS should close the connection with COMPRESSION_ERROR because the HPACK table is out of sync.')
            self._ts.Disk.traffic_out.Content += Testers.ContainsExpression(
                f'timeout event={VC_EVENT_ACTIVE_TIMEOUT}',
                'ATS should time out the incomplete header with the stream active timeout.')
            # The default diags.log check rejects any ERROR:, but this test
            # expects the incomplete header timeout to be reported.
            self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
                timeout_error, 'ATS should log the incomplete header timeout.')
        else:
            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                'stream 1: status=200', 'ATS should proxy the request rather than time out the stream.')
            tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression('GOAWAY', 'ATS should not close the connection.')
            self._ts.Disk.diags_log.Content += Testers.ExcludesExpression(
                timeout_error, 'ATS should not report an incomplete header timeout.')

    def run(self) -> None:
        """Run the test."""
        tr = Test.AddTestRun(self._name)
        self._configure_server(tr)
        self._configure_traffic_server(tr)
        self._configure_client(tr)
        self._configure_expectations(tr)

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        TestHttp2IncompleteHeaderTimeout.counter += 1


# A client that never completes the header block should be timed out at the
# configured timeout, well before the 10 second default.
TestHttp2IncompleteHeaderTimeout(
    'Incomplete header block times out',
    incomplete_header_timeout_in=3,
    path='/incomplete',
    uuid='incomplete_header',
    continuation_delay=None,
    min_elapsed=2.5,
    max_elapsed=7,
    expect_timeout=True).run()

# A client that completes the header block before the timeout expires should be
# proxied normally.
TestHttp2IncompleteHeaderTimeout(
    'CONTINUATION frame arrives before the timeout',
    incomplete_header_timeout_in=5,
    path='/incomplete',
    uuid='incomplete_header',
    continuation_delay=1,
    expect_timeout=False).run()

# The timeout is canceled when the transaction starts, so a slow origin
# response must not be mistaken for an incomplete header block.
TestHttp2IncompleteHeaderTimeout(
    'Timeout is canceled once the transaction starts',
    incomplete_header_timeout_in=2,
    path='/delayed',
    uuid='delayed_response',
    end_headers=True,
    min_elapsed=4.5,
    expect_timeout=False).run()
