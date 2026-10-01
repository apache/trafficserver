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

import pytest

from tools.uranium.services import ATSFactory, Curl, ServiceFactory, wait_for_file_lines


@pytest.mark.parametrize(
    "ceiling,override,resumes", [(None, None, True), (4096, None, False), (None, "-1", False), (None, "1048576", True)])
def test_tls_origin_session_reuse_large(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    ceiling: int | None,
    override: str | None,
    resumes: bool,
) -> None:
    """Honor the ASN.1 session size ceiling, including environment clamping.

    :param ats_factory: Factory owning the origin and proxy ATS instances.
    :param services: Factory owning the HTTP backend.
    :param curl: Transport-aware client.
    :param ceiling: Explicit maximum serialized session size in bytes, or default.
    :param override: Environment record override, or None.
    :param resumes: Whether the second connection must resume the first session.
    """
    backend = services.origin("backend")
    backend.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
            "body": ""
        }, {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "large session test"
        })
    origin = ats_factory.create("origin", enable_tls=True)
    origin.copy_to_ssl("ssl/server-large.pem", "ssl/server-large.key")
    origin.set_ssl_multicert_yaml(
        {"ssl_multicert": [{
            "dest_ip": "*",
            "ssl_cert_name": "server-large.pem",
            "ssl_key_name": "server-large.key"
        }]})
    origin.records.update({"proxy.config.http.cache.http": 0, "proxy.config.ssl.server.session_ticket.enable": 1})
    origin.remap_config.add_line(f"map / http://127.0.0.1:{backend.port}/")
    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / https://127.0.0.1:{origin.https_port}/")
    records = {
        "proxy.config.http.cache.http": 0,
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "ssl.origin_session_cache",
        "proxy.config.ssl.origin_session_cache.enabled": 1,
        "proxy.config.ssl.origin_session_cache.size": 10,
        "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
    }
    if ceiling is not None:
        records["proxy.config.ssl.origin_session_cache.max_session_size"] = ceiling
    ats.records.update(records)
    if override is not None:
        ats.set_environment("PROXY_CONFIG_SSL_ORIGIN_SESSION_CACHE_MAX_SESSION_SIZE", override)
    backend.start()
    origin.start()
    ats.start()
    for _ in range(2):
        result = curl.run_for(ats, f"--insecure https://127.0.0.1:{ats.https_port}/")
        assert result.returncode == 0, result.output
        assert "large session test" in result.stdout
    marker = "reused session to origin" if resumes else "Unable to save SSL session because size"
    wait_for_file_lines(ats.traffic_out, marker, 1)
    ats.stop()
    content = ats.traffic_out.read_text(errors="replace")
    if resumes:
        if override is None:
            assert "new session to origin" in content
            assert "Unable to save SSL session because size" not in content
    else:
        assert "reused session to origin" not in content
    if override is not None:
        assert f"max_session_size of {override} is outside" in ats.diags_log.read_text(errors="replace")
