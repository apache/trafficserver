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
"""Check cache freshness log values after replaying all cache outcomes."""

import re
import sys

from tools.uranium.services import ATSFactory, ServiceFactory, wait_for_file_lines


def test_cache_freshness_fields(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Preserve exact freshness, age, result, and failed-write assertions.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning the replay server and client.
    """
    replay = services.resolve_path("replay/cache-freshness-fields.replay.yaml")
    server = services.verifier_server("server", replay)
    ats = ats_factory.create("ts", enable_cache=True)
    ats.records.update(
        {
            "proxy.config.cache.max_doc_size": 65536,
            "proxy.config.http.cache.http": 1,
            "proxy.config.http.insert_age_in_response": 0,
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.remap_config.add_line(f"map http://example.com/ http://127.0.0.1:{server.http_port}/")
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "cache_freshness_fields",
                        "format": "%<{uuid}cqh> %<cfl> %<cca> %<crc> %<cwr>"
                    }],
                    "logs": [{
                        "filename": "cache_freshness_fields",
                        "format": "cache_freshness_fields",
                        "mode": "ascii"
                    }],
                }
        })
    server.start()
    ats.start()
    client = services.verifier_client("client", replay, http_ports=[ats.http_port])
    result = client.run(timeout=60)
    assert result.returncode == 0, result.output
    log = ats.log_directory / "cache_freshness_fields.log"
    wait_for_file_lines(
        log, r"^(cache-write|cache-hit|negative-revalidation-write|negative-revalidation|failed-cache-write|uncacheable) ", 6)
    result = ats.run(sys.executable, services.resolve_path("verify_cache_freshness_fields.py"), log)
    assert result.returncode == 0, result.output
    ats.stop()
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert not re.search(r"ERROR:|FATAL:|Unrecognized configuration value", diagnostics), diagnostics
