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
"""Verify QUERY retry admission before sending and after not-processed signals."""

import re
import shlex
import sys
from pathlib import Path

import pytest
from tools.uranium.services import ATSFactory, Curl, ServiceFactory, wait_for_metric

from ..h2.origin_test_helpers import configure_h2_origin, finish_origin

BODY = 'SELECT * FROM t WHERE a'


@pytest.mark.parametrize('method,buffering', [('GET', False), ('QUERY', True), ('QUERY', False)])
@pytest.mark.parametrize('failure', ['close', 'tls-read-error', 'tls-handshake-error'])
def test_query_retry(
    ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, method: str, buffering: bool, failure: str
) -> None:
    """Retry GET across origin failures and QUERY only before sending.

    A QUERY retry after upload requires both a not-processed signal and a
    complete replayable body, as covered by test_query_not_processed_retry.
    These HTTP/1 origin failures provide no such signal, so buffering alone
    cannot admit a retry after upload.

    :param ats_factory: Proxy factory.
    :param services: Custom origin factory.
    :param curl: Transport-aware HTTP client.
    :param method: GET without content as the retry control, or QUERY with content.
    :param buffering: Whether ATS buffers QUERY content; this alone cannot admit
        a retry after upload without a not-processed signal.
    :param failure: Origin close or TLS read error after upload, or TLS handshake
        failure before any request bytes are sent.
    """
    port = services.allocate_port()
    ats = ats_factory.create('ts')
    command = [sys.executable, Path(__file__).with_name('query_retry_origin.py'), str(port)]
    tls = failure != 'close'
    before_request = failure == 'tls-handshake-error'
    if tls:
        ssl = Path(__file__).parents[2] / 'tools/ssl'
        command.extend(['--tls', ssl / 'server.pem', ssl / 'server.key', '--upload-log', ats.traffic_out])
    if before_request:
        command.append('--handshake-error')
    origin = services.process('origin', command, ready_port=port)
    ats.records.update(
        {
            'proxy.config.http.request_buffer_enabled': int(buffering),
            'proxy.config.http.connect_attempts_max_retries': 3,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http|http_trans',
            'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
            'proxy.config.ssl.client.alpn_protocols': 'http/1.1',
        }
    )
    scheme = 'https' if tls else 'http'
    ats.remap_config.add_line(f'map / {scheme}://127.0.0.1:{port}/')

    if method == 'QUERY':
        origin.stdout.contains(
            f'request attempt=1 method=QUERY body={re.escape(BODY)}', 'The origin must receive the QUERY and its content.'
        )
        origin.stdout.excludes('request attempt=2 method=', 'ATS must not retry a QUERY after sending it.')
    elif not before_request:
        origin.stdout.contains('request attempt=2 method=GET', 'ATS must retry GET after the origin closes.')
    if before_request:
        origin.stdout.contains('TLS handshake failed before request', 'The first connection must fail before any request.')

    origin.start()
    ats.start()
    content = f"-H 'Content-Type: application/sql' --data-binary {shlex.quote(BODY)} " if method == 'QUERY' else ''
    result = curl.get(ats, '/search', options=f"-s --max-time 30 -o /dev/null -w '%{{http_code}}' -X {method} {content}")
    assert result.returncode == 0, result.output
    assert result.stdout == ('502' if method == 'QUERY' and not before_request else '200'), result.output
    if method == 'QUERY':
        assert origin.stdout_text.count('request headers method=QUERY') == 1, origin.stdout_text
    if failure == 'tls-read-error':
        assert re.search(r'\[handle_server_setup_error, .*VC_EVENT_ERROR\]', ats.traffic_out.read_text())


@pytest.mark.parametrize('mode', ['rst', 'goaway'])
@pytest.mark.parametrize('buffering', [False, True])
def test_query_not_processed_retry(
    ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, mode: str, buffering: bool
) -> None:
    """Replay QUERY content only when the origin reports it was not processed.

    :param ats_factory: Proxy factory.
    :param services: HTTP/2 origin factory.
    :param curl: Transport-aware HTTP client.
    :param mode: REFUSED_STREAM reset or GOAWAY excluding the request stream.
    :param buffering: Whether ATS retains a complete request body for replay.
    """
    directory = Path(__file__).parents[1] / 'h2'
    ssl = Path(__file__).parents[2] / 'tools/ssl'
    port = services.allocate_port()
    ats = configure_h2_origin(
        ats_factory,
        port,
        **{
            'proxy.config.http.request_buffer_enabled': int(buffering),
            'proxy.config.http.connect_attempts_max_retries': 3,
        },
    )
    stop = ats.run_directory / 'stop-origin'
    origin = services.process(
        'origin',
        [
            sys.executable,
            directory / 'safe_retry_origin.py',
            mode,
            str(port),
            ssl / 'server.pem',
            ssl / 'server.key',
            stop,
            '2' if buffering else '1',
            '--method',
            'QUERY',
        ],
        ready_port=port,
    )
    if buffering:
        origin.stdout.contains(
            'retry_succeeded attempts=2 method=QUERY body=request-body', 'The admitted retry must replay the entire QUERY body.'
        )
    else:
        origin.stdout.excludes('request_received attempt=2', 'An unbuffered QUERY must not be retried.')
    origin.start()
    ats.start()
    result = curl.get(ats, '/search', options="-s --max-time 30 -o /dev/null -w '%{http_code}' -X QUERY --data-binary request-body")
    assert result.returncode == 0, result.output
    assert result.stdout == ('200' if buffering else '502'), result.output
    wait_for_metric(ats, 'proxy.process.http.origin.retry_admitted', int(buffering))
    wait_for_metric(ats, 'proxy.process.http.origin.retry_body_unavailable', int(not buffering))
    finish_origin(services, origin, stop)
