'''
Verify that Traffic Server frees HTTP/3 request streams while the connection is still open.
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

from pathlib import Path
import re
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent

# Requests sent for each sequential FIN ordering. Every one of them must be
# reclaimed, so a handful is enough to show the pattern without slowing the test.
REQUESTS_PER_ORDERING = 5

# Requests whose FIN follows the headers by a few milliseconds. Only some of
# them have the FIN handled in the same pass as the end of the response, so
# enough are sent that a build which drops those transactions misses many.
RACE_REQUESTS = 96

ACCESS_LOG_NAME = "h3_stream_reclamation"


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Serve HTTP/3 from the generator plugin, log each request path, and log stream creation and deletion.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts-h3-stream-reclamation", enable_tls=True, enable_quic=True, enable_cache=False)
    ats.set_startup_timeout(60)
    ats.add_default_ssl_files()
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        )
    )
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "quic_stream|http3",
            "proxy.config.quic.server.stateless_retry_enabled": 0,
            "proxy.config.quic.initial_max_streams_bidi_in": 1000,
            "proxy.config.log.max_secs_per_buffer": 1,
        }
    )
    ats.remap_config.add_line("map / http://127.0.0.1/ @plugin=generator.so")
    ats.set_logging_yaml(
        {
            "logging": {
                "formats": [{"name": "request_path", "format": "/%<cqup>"}],
                "logs": [{"filename": ACCESS_LOG_NAME, "format": "request_path"}],
            }
        }
    )
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Create the aioquic client that varies when each request stream is finished.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "py_h3_client" / "h3_stream_reclamation_client.py",
            "--addr",
            f"127.0.0.1:{_ats.https_port}",
            "--authority",
            f"reclaim.example.com:{_ats.https_port}",
            "--server-name",
            "reclaim.example.com",
            "--count",
            str(REQUESTS_PER_ORDERING),
            "--race-count",
            str(RACE_REQUESTS),
            "--access-log",
            _ats.log_directory / f"{ACCESS_LOG_NAME}.log",
        ),
    )


def test_h3_stream_reclamation(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Every request is logged and its stream deleted, wherever its FIN falls relative to the response.

    When the response finishes before the request's FIN arrives, the stream is
    only reclaimable once that FIN is read. A stream that is not reclaimed then
    stays allocated until the connection closes, so a client that keeps one
    connection open can grow it without bound. When the FIN is read in the same
    pass as the end of the response, the stream must not be reclaimed before
    the transaction learns that its response was sent, or the transaction never
    finishes and the request is never logged.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    if not ats_factory.has_feature("TS_USE_QUIC"):
        pytest.skip("ATS was built without QUIC")
    _ats = configure_ats(ats_factory)
    _client = configure_client(services, _ats=_ats)

    _ats.start()
    result = _client.run(timeout=90)
    assert result.returncode == 0, result.output

    unlogged = re.findall(r"^unlogged (\S+)$", result.stdout, re.MULTILINE)
    assert not unlogged, f"{len(unlogged)} requests not logged while the connection was open: {', '.join(unlogged)}"

    streams = re.findall(r"^stream ([0-9]+) (\S+)$", result.stdout, re.MULTILINE)
    assert len(streams) == 2 * REQUESTS_PER_ORDERING + RACE_REQUESTS, result.output

    # The client waited before closing, so a stream reclaimed while the
    # connection was open is already logged. Streams left at close are freed
    # without this log line.
    debug_output = _ats.traffic_out.read_text(errors="replace")
    deleted = set(re.findall(r"\(quic_stream\) deleted stream ([0-9]+)", debug_output))
    leaked = [f"{stream_id} ({ordering})" for stream_id, ordering in streams if stream_id not in deleted]
    assert not leaked, f"request streams not deleted while the connection was open: {', '.join(leaked)}"
