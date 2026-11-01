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
"""Verify QUERY content is not lost by retrying after an origin closes."""

from pathlib import Path
import re
import shlex
import sys

import pytest

from tools.uranium.services import ATSFactory, Curl, ServiceFactory

BODY = 'SELECT * FROM t WHERE a'


@pytest.mark.parametrize('method,buffering', [('GET', False), ('QUERY', True), ('QUERY', False)])
def test_query_retry(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, method: str, buffering: bool) -> None:
    """Retry GET after an origin closes, but never replay QUERY without content.

    :param ats_factory: Proxy factory.
    :param services: Custom origin factory.
    :param curl: Transport-aware HTTP client.
    :param method: Request method; GET is the safe retry control.
    :param buffering: Whether ATS buffers the request content.
    """
    port = services.allocate_port()
    origin = services.process(
        'origin', [sys.executable, Path(__file__).with_name('query_retry_origin.py'),
                   str(port)], ready_port=port)
    ats = ats_factory.create('ts')
    ats.records.update(
        {
            'proxy.config.http.request_buffer_enabled': int(buffering),
            'proxy.config.http.connect_attempts_max_retries': 3,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http_trans',
        })
    ats.remap_config.add_line(f'map / http://127.0.0.1:{port}/')

    if method == 'QUERY':
        origin.stdout.contains(
            f'request attempt=1 method=QUERY body={re.escape(BODY)}', 'The origin must receive the QUERY and its content.')
        origin.stdout.excludes('request attempt=2 method=', 'ATS must not retry a QUERY after sending it.')
    else:
        origin.stdout.contains('request attempt=2 method=GET', 'ATS must retry GET after the origin closes.')

    origin.start()
    ats.start()
    content = f"-H 'Content-Type: application/sql' --data-binary {shlex.quote(BODY)} " if method == 'QUERY' else ''
    result = curl.get(ats, '/search', options=f"-s --max-time 30 -o /dev/null -w '%{{http_code}}' -X {method} {content}")
    assert result.returncode == 0, result.output
    assert result.stdout == ('502' if method == 'QUERY' else '200'), result.output
