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

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import sys

import pytest

from tools.uranium.services import ATSFactory, Curl, ServiceFactory

PATHS = 10
CONCURRENCY = 6
CLIENT_MAX_TIME = 10


def test_compress_transform_cache_write(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Concurrent transforms of a cached identity object must all complete.

    Each request that accepts br hits the stored uncompressed object, runs it
    through compress.so, and opens a cache write for the compressed copy. With
    several of these requests at the same time the open write retries on the
    write lock. If the cache-to-transform tunnel completes during the retry,
    HTTP_TUNNEL_EVENT_DONE reaches HttpSM::state_cache_open_write. That
    handler must not cancel the pending open write, or the transaction never
    sends a response.

    :param ats_factory: Factory for isolated proxy instances.
    :param services: Factory owning the origin.
    :param curl: Transport-aware client.
    """
    directory = Path(__file__).parent
    port = services.allocate_port()
    origin = services.process(
        "origin",
        (sys.executable, directory / "compress_large_origin.py", "--port", str(port)),
        ready_port=port,
    )
    ats = ats_factory.create("ts", enable_cache=True)
    if not ats.plugin_exists("compress.so"):
        pytest.skip("compress.so is required")
    if not ats.has_feature("TS_HAS_BROTLI"):
        pytest.skip("compress.so with brotli support is required")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "compress",
            # Make the open write for the transformed copy wait long enough
            # for the transform tunnel to complete first.
            "proxy.config.http.cache.max_open_read_retries": 5,
            "proxy.config.http.cache.open_read_retry_time": 100,
            # compress.so adds Vary: Accept-Encoding to the identity response.
            # Let the br requests use that stored object, so that the transform
            # reads from the cache.
            "proxy.config.http.cache.ignore_accept_encoding_mismatch": 1,
        })
    config = directory / "etc" / "cache-true-transform-write.config"
    ats.copy_to_config(config)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{port}/ @plugin=compress.so @pparam={ats.config_directory / config.name}")
    origin.start()
    ats.start()

    def fetch(url: str, accept_encoding: str | None) -> tuple[str, str]:
        """Return the status code and Content-Encoding of a completed transfer.

        A transfer that curl did not complete, for example after a timeout or
        a truncated body, returns the curl exit code as the status.
        """
        header = f"--header 'Accept-Encoding: {accept_encoding}' " if accept_encoding else ""
        result = curl.run_for(
            ats, f"--http1.1 --silent --output /dev/null --max-time {CLIENT_MAX_TIME} {header}"
            f"--write-out '%{{http_code}} %header{{content-encoding}}' {url}",
            timeout=CLIENT_MAX_TIME + 5)
        if result.returncode != 0:
            return f"curl exit {result.returncode}", ""
        status, _, encoding = result.stdout.strip().partition(" ")
        return status, encoding

    probe = f"http://127.0.0.1:{ats.http_port}/probe.json"
    assert fetch(probe, None) == ("200", "")
    assert fetch(probe, "br") == ("200", "br")

    stalled = []
    with ThreadPoolExecutor(max_workers=CONCURRENCY) as pool:
        for i in range(PATHS):
            url = f"http://127.0.0.1:{ats.http_port}/item-{i}.json"
            assert fetch(url, None) == ("200", "")
            results = list(pool.map(lambda _: fetch(url, "br"), range(CONCURRENCY)))
            stalled += [(url, result) for result in results if result != ("200", "br")]

    assert not stalled, f"{len(stalled)} of {PATHS * CONCURRENCY} transformed cache hits did not complete: {stalled[:3]}"
    assert ats.is_running
