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

import re

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the final clear-text origin.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "curl test"
        },
    )
    return origin


def configure_tls_origin(ats_factory: ATSFactory, name: str, origin: OriginServer) -> ATS:
    """Create an ATS TLS endpoint that issues session tickets.

    Each Traffic Server process generates its own random ticket key, so two
    of these endpoints cannot resume each other's sessions.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service name within this test.
    :param origin: Clear-text origin behind the TLS endpoint.
    """

    ats = ats_factory.create(name, enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.http.cache.http": 0,
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.ssl.server.session_ticket.enable": 1,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.http_port}")
    return ats


def configure_proxy(ats_factory: ATSFactory, *, _ts_a: ATS, _ts_b: ATS) -> ATS:
    """Create the ATS instance whose outbound session reuse is logged.

    Both TLS endpoints listen on 127.0.0.1 without SNI, so they share one
    origin session cache entry: a session issued by one is offered to the
    other, which must reject it.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param _ts_a: TLS endpoint behind the /a path.
    :param _ts_b: TLS endpoint behind the /b path.
    """

    ats = ats_factory.create("proxy", enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.http.cache.http": 0,
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            # Every request needs its own origin handshake.
            "proxy.config.http.keep_alive_enabled_out": 0,
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
        })
    ats.remap_config.add_lines((
        f"map /a https://127.0.0.1:{_ts_a.https_port}",
        f"map /b https://127.0.0.1:{_ts_b.https_port}",
    ))
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "reuse",
                        "format": "%<cquup> pqssr=%<pqssr>",
                    }],
                    "logs": [{
                        "filename": "reuse",
                        "format": "reuse"
                    }],
                }
        })
    return ats


def request(ats: ATS, path: str, *, _curl: Curl) -> None:
    """Send one request through the proxy on a new client connection.

    :param ats: Proxy under test.
    :param path: Request path selecting the TLS endpoint.
    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.run_for(ats, f"--insecure 'https://127.0.0.1:{ats.https_port}{path}'")
    assert result.returncode == 0, result.output
    assert "curl test" in result.stdout


def metric(ats: ATS, name: str) -> int:
    """Read one integer metric.

    :param ats: Traffic Server instance to query.
    :param name: Metric name.
    """

    result = ats.traffic_ctl("metric", "get", name)
    assert result.returncode == 0, result.output
    return int(result.stdout.split()[-1])


def test_tls_origin_session_reuse_log(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """pqssr logs 1 only when the origin accepts the offered session.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ts_a = configure_tls_origin(ats_factory, "ts_a", _origin)
    _ts_b = configure_tls_origin(ats_factory, "ts_b", _origin)
    _proxy = configure_proxy(ats_factory, _ts_a=_ts_a, _ts_b=_ts_b)

    _origin.start()
    for ats in (_ts_a, _ts_b, _proxy):
        ats.start()

    # The first request caches a session from ts_a and the second resumes it.
    # The third offers that session to ts_b, which falls back to a full
    # handshake.
    for path in ("/a", "/a", "/b"):
        request(_proxy, path, _curl=curl)

    log = wait_for_file_lines(_proxy.log_directory / "reuse.log", r"^\S+ pqssr=\d$", 3)
    entries = re.findall(r"^(\S+) pqssr=(\d)$", log, re.MULTILINE)

    assert metric(_proxy, "proxy.process.ssl.ssl_origin_session_cache_hit") == 2, log
    assert metric(_proxy, "proxy.process.ssl.origin_session_reused") == 1, log
    assert entries == [("/a", "0"), ("/a", "1"), ("/b", "0")], log
