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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


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


def configure_tls(ats: ATS, *, reuse: bool, debug: bool = False) -> None:
    """Apply the shared TLS session settings to one ATS instance.

    :param ats: Traffic Server instance configured or queried by this step.
    :param reuse: Reuse used by this test step.
    :param debug: Debug used by this test step.
    """

    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.http.cache.http": 0,
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.ssl.server.session_ticket.enable": 1,
            "proxy.config.ssl.origin_session_cache.enabled": int(reuse),
            "proxy.config.ssl.origin_session_cache.size": 1,
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.diags.debug.enabled": int(debug),
            "proxy.config.diags.debug.tags": "ssl.origin_session_cache",
        })


def configure_tls_origin(ats_factory: ATSFactory, name: str, *, reuse: bool) -> ATS:
    """Create an ATS TLS endpoint in front of the clear-text origin.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service or case name within this test.
    :param reuse: Reuse used by this test step.
    """

    ats = ats_factory.create(name, enable_tls=True)
    configure_tls(ats, reuse=reuse)
    return ats


def configure_proxy(ats_factory: ATSFactory, name: str, *, reuse: bool) -> ATS:
    """Create an ATS instance whose outbound TLS cache is under test.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service or case name within this test.
    :param reuse: Reuse used by this test step.
    """

    ats = ats_factory.create(name, enable_tls=True)
    configure_tls(ats, reuse=reuse, debug=True)
    return ats


def configure_remaps(*, _origin: OriginServer, _ts1: ATS, _ts2: ATS, _ts3: ATS, _ts4: ATS) -> None:
    """Connect the four ATS instances into cached and disabled pairs.

    :param _origin: Test-local origin configured by the test.
    :param _ts1: Test-local ts1 configured by the test.
    :param _ts2: Test-local ts2 configured by the test.
    :param _ts3: Test-local ts3 configured by the test.
    :param _ts4: Test-local ts4 configured by the test.
    """

    _ts1.remap_config.add_line(f"map / http://127.0.0.1:{_origin.http_port}")
    _ts2.remap_config.add_lines(
        (
            f"map /reuse_session https://127.0.0.1:{_ts1.https_port}",
            f"map /remove_oldest https://127.0.1.1:{_ts1.https_port}",
        ))
    _ts3.remap_config.add_line(f"map / http://127.0.0.1:{_origin.http_port}")
    _ts4.remap_config.add_line(f"map / https://127.0.0.1:{_ts3.https_port}")


def request_twice(ats: ATS, path: str = "", *, _curl: Curl) -> None:
    """Use two client connections to create two outbound TLS sessions.

    :param _curl: Test-local curl configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    :param path: Resource or file path used by this operation.
    """

    for _ in range(2):
        result = _curl.run_for(
            ats,
            f"--insecure 'https://127.0.0.1:{ats.https_port}/{path}'",
        )
        assert result.returncode == 0, result.output
        assert "curl test" in result.stdout


def test_tls_origin_session_reuse(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The outbound TLS session cache reuses, evicts, and disables sessions.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ts1 = configure_tls_origin(ats_factory, "ts1", reuse=True)
    _ts2 = configure_proxy(ats_factory, "ts2", reuse=True)
    _ts3 = configure_tls_origin(ats_factory, "ts3", reuse=True)
    _ts4 = configure_proxy(ats_factory, "ts4", reuse=False)
    configure_remaps(_origin=_origin, _ts1=_ts1, _ts2=_ts2, _ts3=_ts3, _ts4=_ts4)

    _origin.start()
    for ats in (_ts1, _ts2, _ts3, _ts4):
        ats.start()

    request_twice(_ts2, "reuse_session", _curl=curl)
    request_twice(_ts2, "remove_oldest", _curl=curl)
    enabled_log = _ts2.traffic_out.read_text(errors="replace")
    assert "new session to origin" in enabled_log
    assert "reused session to origin" in enabled_log
    assert "remove oldest session" in enabled_log

    request_twice(_ts4, _curl=curl)
    disabled_log = _ts4.traffic_out.read_text(errors="replace")
    assert "new session to origin" in disabled_log
    assert "reused session to origin" not in disabled_log
