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
"""Shared setup and metric assertions for HTTP/2 origin regression tests."""

import sys
import time
from pathlib import Path
from tools.uranium.services import ATS, ATSFactory, ServiceFactory, ProcessService, assert_matches_gold

DIRECTORY = Path(__file__).parent


def configure_h2_origin(ats_factory: ATSFactory, port: int, *, disable_log_checks: bool = False, **records: object) -> ATS:
    """Configure one isolated proxy with a reusable HTTP/2 origin connection.

    :param ats_factory: Factory owning proxy cleanup.
    :param port: TLS origin listener port.
    :param disable_log_checks: Replace default error checks with explicit assertions for deliberate protocol errors.
    :param records: Additional record values overriding the shared defaults.
    """
    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False, disable_log_checks=disable_log_checks)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.ssl.client.alpn_protocols": "h2,http/1.1",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.http.server_session_sharing.pool": "thread",
            "proxy.config.http.server_session_sharing.match": "ip",
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|http2",
            **records,
        }
    )
    ats.remap_config.add_line(f"map / https://127.0.0.1:{port}")
    return ats


def wait_for_metrics(ats: ATS, gold: str, names: list[str]) -> None:
    """Poll derived counters until the complete expected snapshot is available.

    :param ats: Running proxy to query.
    :param gold: Filename under this directory's gold subdirectory.
    :param names: Fully qualified metric names.
    """
    deadline = time.monotonic() + 10
    while True:
        result = ats.traffic_ctl("metric", "get", *names)
        assert result.returncode == 0, result.output
        try:
            assert_matches_gold(result.stdout, DIRECTORY / "gold" / gold)
            return
        except AssertionError:
            if time.monotonic() >= deadline:
                raise
        time.sleep(0.1)


def finish_origin(services: ServiceFactory, origin: ProcessService, stop: Path) -> None:
    """Ask a cooperative origin to validate its counts and exit.

    :param services: Factory for the lifecycle helper.
    :param origin: Running origin whose successful exit is required.
    :param stop: Stop marker shared with the origin.
    """
    result = services.process("finish-origin", [sys.executable, DIRECTORY / "origin_lifecycle.py", stop]).run(timeout=15)
    assert result.returncode == 0, result.output
    result = origin.wait(timeout=15)
    assert result.returncode == 0, result.output
