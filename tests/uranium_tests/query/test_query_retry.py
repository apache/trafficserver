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
"""Verify that ATS does not retry a QUERY after it sent the request and its
content to an origin that then closed the connection. The request buffering
setting does not change this."""

import re
import shlex
import sys
from pathlib import Path

import pytest
from tools.uranium.services import ATSFactory, Curl, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
BODY = 'SELECT * FROM t WHERE a'


@pytest.mark.parametrize(
    ("method", "buffering"),
    (
        # GET shows that the close of the origin causes ATS to retry a safe method.
        pytest.param('GET', False, id="get"),
        pytest.param('QUERY', True, id="query-buffered"),
        pytest.param('QUERY', False, id="query-unbuffered"),
    ),
)
def test_query_retry(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, method: str, buffering: bool) -> None:
    """Send one request through an origin that closes the first connection.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the origin process.
    :param curl: Transport-aware curl command runner.
    :param method: HTTP method that the client sends.
    :param buffering: Value of ``proxy.config.http.request_buffer_enabled``.
    """
    origin_port = services.allocate_port()
    origin = services.process(
        "origin",
        (sys.executable, TEST_DIRECTORY / "query_retry_origin.py", str(origin_port)),
        ready_port=origin_port,
    )

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            'proxy.config.http.request_buffer_enabled': 1 if buffering else 0,
            'proxy.config.http.connect_attempts_max_retries': 3,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http_trans',
        }
    )
    ats.remap_config.add_line(f'map / http://127.0.0.1:{origin_port}/')

    if method == 'QUERY':
        origin.stdout.contains(
            f'request attempt=1 method=QUERY body={re.escape(BODY)}', 'The origin must receive the QUERY and its content.'
        )
        origin.stdout.excludes('request attempt=2 method=', 'ATS must not retry a QUERY after sending it.')
        expected_status = '502'
    else:
        origin.stdout.contains(f'request attempt=2 method={method}', f'ATS must retry a {method} after the origin closes.')
        expected_status = '200'

    origin.start()
    ats.start()

    content = f"--header 'Content-Type: application/sql' --data-binary {shlex.quote(BODY)} " if method == 'QUERY' else ''
    result = curl.run_for(
        ats,
        f"--silent --max-time 30 --output /dev/null --write-out '%{{http_code}}\\n' --request {method} {content}"
        f"http://127.0.0.1:{ats.http_port}/search",
        timeout=40,
    )
    assert result.returncode == 0, result.output
    assert re.search(f'^{expected_status}$', result.stdout, re.MULTILINE), result.output

    # Stop the origin before its output is examined, so that the exclusion
    # check sees every request that ATS sent.
    origin.close()
