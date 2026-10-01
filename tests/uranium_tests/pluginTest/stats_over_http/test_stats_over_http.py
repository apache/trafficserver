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

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Load the plugin at its conventional _stats endpoint.

    :param ats_factory: Factory for isolated ATS processes.
    """

    ats = ats_factory.create("ts")
    if not ats.plugin_exists("stats_over_http.so"):
        pytest.skip("stats_over_http.so is not installed")
    ats.plugin_config.add_line("stats_over_http.so _stats")
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "unmapped_url",
                        "format": "%<cquuc>"
                    }],
                    "logs": [{
                        "filename": "stats_over_http_url",
                        "format": "unmapped_url"
                    }],
                }
        })
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "stats_over_http",
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    return ats


def fetch(path: str = "/_stats", accept: str | None = None, *, _ats: ATS, _curl: Curl) -> str:
    """Fetch one stats representation and return its response body.

    :param path: stats_over_http endpoint path.
    :param accept: Optional HTTP Accept header value.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    headers = {"Accept": accept} if accept is not None else None
    result = _curl.get(_ats, path, headers=headers, options="--silent --show-error --http1.1")
    assert result.returncode == 0, result.output
    return result.stdout


def verify_prometheus_v1(output: str, *, parsed: bool = False) -> None:
    """Check gauge and counter families in raw or parsed Prometheus output.

    :param output: Prometheus representation to validate.
    :param parsed: Whether the Python client has normalized the representation.
    """

    assert "HELP proxy_process_http2_current_client_connections" in output
    assert "TYPE proxy_process_http2_current_client_connections gauge" in output
    assert "proxy_process_http2_current_client_connections 0" in output
    assert "HELP proxy_process_http_delete_requests" in output
    assert "TYPE proxy_process_http_delete_requests counter" in output
    counter = "proxy_process_http_delete_requests_total 0" if parsed else "proxy_process_http_delete_requests 0"
    assert counter in output


def verify_prometheus_v2(output: str, *, parsed: bool = False) -> None:
    """Check v2 metric families, normalized names, and extracted labels.

    :param output: Prometheus representation to validate.
    :param parsed: Whether the Python client has normalized the representation.
    """

    suffix = "_total" if parsed else ""
    required = (
        "# TYPE proxy_process_http_requests counter",
        f'proxy_process_http_requests{suffix}{{method="delete"}}',
        f'proxy_process_http_requests{suffix}{{method="extension_method"}}',
        f'proxy_process_http_requests{suffix}{{direction="incoming"}}',
        f"proxy_process_http_completed_requests{suffix}",
        f'proxy_process_http_disallowed_continue{suffix}{{method="post",status="100"}}'
        if parsed else 'proxy_process_http_disallowed_continue{method="post", status="100"}',
        'proxy_process_cache_volume_lookup_active{volume="0"}',
        'proxy_process_eventloop_count{le="',
    )
    for expression in required:
        assert expression in output
    assert 'method="completed"' not in output
    if parsed:
        for expression in (
                '# TYPE proxy_process_http_responses counter',
                'proxy_process_http_responses_total{direction="incoming"}',
                'proxy_process_http_responses_total{status="2xx"}',
                'proxy_process_http_cache_ims_total{result="miss"}',
                'proxy_process_cache_volume_lookup_success_total{volume="0"}',
        ):
            assert expression in output


def parse_metrics(path: str, *arguments: str, _ats: ATS, _services: ServiceFactory) -> str:
    """Run the Prometheus client parser against one ATS endpoint.

    :param path: stats_over_http endpoint path.
    :param arguments: Additional parser command-line arguments.

    :param _ats: Test-local ats configured by the test.
    :param _services: Test-local services configured by the test.
    """

    process = _services.process(
        f"prometheus-parser-{len(arguments)}",
        [
            sys.executable,
            TEST_DIRECTORY / "prometheus_stats_ingester.py",
            *arguments,
            f"http://127.0.0.1:{_ats.http_port}{path}",
        ],
    )
    return process.run().stdout


def test_stats_over_http(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """stats_over_http exports valid JSON, CSV, Prometheus v1, and Prometheus v2 representations.

    :param ats_factory: Factory for isolated ATS processes.
    :param services: Factory for supporting test services.
    :param curl: Curl command helper.
    """
    _ats = configure_ats(ats_factory)

    if curl.uses_uds:
        pytest.skip("stats_over_http does not support the curl UDS mode")
    _ats.start()

    json_default = fetch(_ats=_ats, _curl=curl)
    assert '{ "global": {' in json_default
    assert '"proxy.process.http.delete_requests": "0",' in json_default
    assert "proxy.process.http.delete_requests,0" in fetch(accept="text/csv", _ats=_ats, _curl=curl)
    prometheus = fetch(accept="text/plain; version=0.0.4", _ats=_ats, _curl=curl)
    verify_prometheus_v1(prometheus)
    prometheus_v2 = fetch(accept="text/plain; version=2.0.0", _ats=_ats, _curl=curl)
    verify_prometheus_v2(prometheus_v2)

    assert '{ "global": {' in fetch("/_stats/json", _ats=_ats, _curl=curl)
    assert "proxy.process.http.delete_requests,0" in fetch("/_stats/csv", _ats=_ats, _curl=curl)
    path_prometheus = fetch("/_stats/prometheus", _ats=_ats, _curl=curl)
    verify_prometheus_v1(path_prometheus)
    path_prometheus_v2 = fetch("/_stats/prometheus_v2", _ats=_ats, _curl=curl)
    verify_prometheus_v2(path_prometheus_v2)
    assert "proxy_process_http_delete_requests 0" in fetch("/_stats/prometheus", "text/csv", _ats=_ats, _curl=curl)

    verify_prometheus_v1(parse_metrics("/_stats/prometheus", _ats=_ats, _services=services), parsed=True)
    parsed_v2 = parse_metrics(
        "/_stats/prometheus_v2", "--validate-v2-format", "--strict-family-metadata", _ats=_ats, _services=services)
    verify_prometheus_v2(parsed_v2, parsed=True)

    access_log = wait_for_file_lines(_ats.log_directory / "stats_over_http_url.log", r"/_stats", 1)
    assert f"http://127.0.0.1:{_ats.http_port}/_stats" in access_log
    assert "http:///" not in access_log
