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

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent
REPLAY_FILE = TEST_DIRECTORY / "replays" / "h2_malformed_request_logging.replay.yaml"
MALFORMED_CLIENT = TEST_DIRECTORY / "malformed_h2_request_client.py"

MALFORMED_H2_REQUEST_LOGGING_CASES = (
    ("connect-missing-authority", "malformed-connect", "CONNECT", "/"),
    ("get-missing-path", "malformed-get-missing-path", "GET", "https://missing-path.example/"),
    (
        "get-connection-header",
        "malformed-get-connection",
        "GET",
        "https://bad-connection.example/bad-connection",
    ),
)


def configure_server(services: ServiceFactory) -> VerifierServer:
    """Create the origin for the healthy control requests.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server("malformed-request-server", REPLAY_FILE)


def configure_ats(ats_factory: ATSFactory, *, _server: VerifierServer) -> ATS:
    """Configure an HTTP/2-only listener and the malformed-request log format.

    :param _server: Test-local server configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.storage_config.add_line("")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|hpack|http2",
            "proxy.config.http.server_ports": f"{ats.https_port}:ssl",
            "proxy.config.http.connect_ports": _server.http_port,
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_server.http_port}/")
    ats.allow_private_connect(("CONNECT", "GET"))
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats":
                        [
                            {
                                "name": "malformed_h2_request",
                                "format": ("uuid=%<{uuid}cqh> cqpv=%<cqpv> cqhm=%<cqhm> "
                                           "crc=%<crc> sstc=%<sstc> pqu=%<pqu>"),
                            }
                        ],
                    "logs": [{
                        "filename": "squid",
                        "format": "malformed_h2_request",
                        "mode": "ascii"
                    }],
                }
        })
    return ats


def configure_valid_client(services: ServiceFactory, *, _ats: ATS) -> ProcessService:
    """Create the healthy GET and CONNECT control client.

    :param _ats: Test-local ats configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_client("valid-request-client", REPLAY_FILE, https_ports=[_ats.https_port])


def malformed_request(scenario: str, *, _ats: ATS, _services: ServiceFactory) -> str:
    """Run the raw-frame client for one malformed shape.

    :param _ats: Test-local ats configured by the test.
    :param _services: Test-local services configured by the test.
    :param scenario: Scenario used by this test step.
    """

    process = _services.process(
        f"malformed-client-{scenario}",
        [sys.executable, MALFORMED_CLIENT, str(_ats.https_port), scenario],
    )
    result = process.run(timeout=10)
    assert re.search(r"Received (RST_STREAM on stream 1 with error code 1|GOAWAY with error code [01])", result.stdout)
    return result.output


def validate_logs(*, _ats: ATS) -> None:
    """Check malformed and healthy transactions in the access log.

    :param _ats: Test-local ats configured by the test.
    """

    wait_for_file_lines(
        _ats.log_directory / "squid.log",
        "crc=ERR_INVALID_REQ",
        len(MALFORMED_H2_REQUEST_LOGGING_CASES),
    )
    squid_log = wait_for_file_lines(
        _ats.log_directory / "squid.log",
        r"uuid=valid-get",
        1,
    )
    for _scenario, uuid, method, url in MALFORMED_H2_REQUEST_LOGGING_CASES:
        expected = (rf"uuid={uuid} cqpv=http/2 cqhm={method} "
                    rf"crc=ERR_INVALID_REQ sstc=0 pqu={re.escape(url)}")
        assert re.search(expected, squid_log)
    assert re.search(r"uuid=valid-get cqpv=http/2 cqhm=GET ", squid_log)
    if "uuid=valid-connect" in squid_log:
        assert re.search(r"uuid=valid-connect .*crc=ERR_INVALID_REQ", squid_log) is None
    assert re.search(r"uuid=valid-get .*crc=ERR_INVALID_REQ", squid_log) is None


def test_h2_malformed_request_logging(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Malformed HTTP/2 requests receive ERR_INVALID_REQ access-log records.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _server = configure_server(services)
    _ats = configure_ats(ats_factory, _server=_server)
    _valid_client = configure_valid_client(services, _ats=_ats)

    _server.start()
    _ats.start()
    for scenario, _uuid, _method, _url in MALFORMED_H2_REQUEST_LOGGING_CASES:
        malformed_request(scenario, _ats=_ats, _services=services)
    _valid_client.run()
    server_output = _server.output
    for _scenario, uuid, _method, _url in MALFORMED_H2_REQUEST_LOGGING_CASES:
        assert f"uuid: {uuid}" not in server_output
    assert re.search(r"GET /get HTTP/1\.1\nuuid: valid-connect", server_output)
    assert re.search(r"GET /valid-get HTTP/1\.1\n(?:.*\n)*uuid: valid-get", server_output)
    validate_logs(_ats=_ats)
    assert "recv headers malformed request" in _ats.traffic_out.read_text(errors="replace")
