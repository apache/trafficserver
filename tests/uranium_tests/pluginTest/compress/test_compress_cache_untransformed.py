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

from pathlib import Path
import sys

import pytest

from tools.uranium.services import ATSFactory, Curl, ServiceFactory


@pytest.mark.parametrize("mode", ["continue", "early-hints"])
def test_compress_cache_untransformed(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, mode: str) -> None:
    """Interim responses must not leave stale header bytes in the cache tunnel.

    :param ats_factory: Factory for isolated proxy instances.
    :param services: Factory owning the interim-response origin.
    :param curl: Transport-aware client.
    :param mode: A 100 before a POST response, or 103 before a ranged 308.
    """
    directory = Path(__file__).parent
    port = services.allocate_port()
    origin = services.process(
        "origin",
        (sys.executable, directory / "compress_100_continue_origin.py", "--port", str(port), "--mode", mode),
        ready_port=port,
    )
    ats = ats_factory.create("ts", enable_cache=True)
    if not ats.plugin_exists("compress.so"):
        pytest.skip("compress.so is required")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|compress|http_tunnel|http_range",
            # Process the origin's 100, not one synthesized by ATS.
            "proxy.config.http.send_100_continue_response": 0,
            "proxy.config.http.cache.post_method": 1,
            "proxy.config.http.cache.range.write": 1,
        })
    config = directory / "etc" / "compress-cache-false.config"
    ats.copy_to_config(config)
    ats.remap_config.add_line(f"map / http://127.0.0.1:{port}/ @plugin=compress.so @pparam={ats.config_directory / config.name}")
    origin.start()
    ats.start()
    if mode == "continue":
        arguments = (
            "--http1.1 --silent --output /dev/null --request POST --header 'Accept-Encoding: gzip' "
            "--header 'Expect: 100-continue' --expect100-timeout 0 --data 'test body data' "
            f"http://127.0.0.1:{ats.http_port}/test/resource.js")
    else:
        # Exercise the range transform alongside the untransformed cache write.
        arguments = (
            "--http1.1 --silent --output /dev/null --header 'Range: bytes=0-64' "
            f"http://127.0.0.1:{ats.http_port}/early-hints/redirect")
    result = curl.run_for(ats, arguments)
    assert result.returncode == 0, result.output
    assert ats.is_running
