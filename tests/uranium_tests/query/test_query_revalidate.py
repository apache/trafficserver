'''
An origin that closes the first connection after reading the request, without
responding, and answers every later request with a 200.
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
"""Verify that ATS revalidates a stale QUERY response with a conditional QUERY.

A replay cannot make this check, because Proxy Verifier accepts a 304 response
where the replay expects a 200 response.
"""

import re
import sys
import time
from pathlib import Path

from tools.uranium.services import ATSFactory, Curl, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def test_query_revalidate(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Send the same QUERY before and after its cached response becomes stale.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the origin process.
    :param curl: Transport-aware curl command runner.
    """
    origin_port = services.allocate_port()
    origin = services.process(
        "origin",
        (sys.executable, TEST_DIRECTORY / "query_revalidate_origin.py", str(origin_port)),
        ready_port=origin_port,
    )

    ats = ats_factory.create("ts", enable_cache=True)
    ats.records.update(
        {
            'proxy.config.http.cache.query_method': 1,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http_trans|http_cache',
        }
    )
    ats.remap_config.add_line(f'map / http://127.0.0.1:{origin_port}/')

    origin.stdout.contains('request method=QUERY conditional=False', 'The first QUERY must reach the origin.')
    origin.stdout.contains('request method=QUERY conditional=True', 'ATS must revalidate the stale entry with If-None-Match.')

    origin.start()
    ats.start()

    def query() -> str:
        result = curl.run_for(
            ats,
            "--silent --max-time 30 --write-out '\\n%{http_code}\\n' --request QUERY "
            "--header 'Content-Type: application/sql' --data-binary 'select-r' "
            f"http://127.0.0.1:{ats.http_port}/search",
            timeout=40,
        )
        assert result.returncode == 0, result.output
        return result.stdout

    assert re.search(r'^answer\n200$', query(), re.MULTILINE)

    # The lifetime of the cached response is one second.
    time.sleep(3)

    # The client did not send a conditional request, so it must get the cached
    # content and not the 304 response of the origin.
    assert re.search(r'^answer\n200$', query(), re.MULTILINE)

    # The 304 response gave the cached entry a new lifetime, so this request
    # does not reach the origin.
    assert re.search(r'^answer\n200$', query(), re.MULTILINE)

    origin.close()
    assert origin.stdout_text.count('request method=QUERY') == 2, origin.stdout_text
