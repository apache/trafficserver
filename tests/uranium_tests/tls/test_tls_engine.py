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
"""Exercise the sample asynchronous OpenSSL engine."""
import re
import shutil
import subprocess

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines


def configure_engine(ats_factory: ATSFactory, services: ServiceFactory) -> tuple[ATS, OriginServer]:
    """Configure the 9.2 asynchronous engine and a small HTTP origin.

    :param ats_factory: Factory for the TLS proxy.
    :param services: Factory for the HTTP origin.
    """
    if shutil.which("openssl") is None:
        pytest.skip("OpenSSL 1.1.1 or newer is required")
    version = subprocess.check_output(["openssl", "version"], text=True)
    match = re.search(r"OpenSSL (\d+)\.(\d+)\.(\d+)", version)
    if not match or tuple(map(int, match.groups())) < (1, 1, 1):
        pytest.skip("The async engine requires OpenSSL 1.1.1 or newer")
    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nuuid: basic\r\n\r\n"}, {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
            "body": "ok"
        })
    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.copy_custom_plugin("{AtsTestPluginsDir}/async_engine.so")
    ats.write_config_file(
        "load_engine.cnf", "openssl_conf = openssl_init\n[openssl_init]\nengines = engine_section\n"
        "[engine_section]\nasync = async_section\n[async_section]\n"
        f"dynamic_path = {services.resolve_path('{AtsTestPluginsDir}/async_engine.so')}\n"
        "engine_id = async-test\ndefault_algorithms = RSA\ninit = 1\n")
    ats.records.update(
        {
            "proxy.config.ssl.engine.conf_file": str(ats.config_directory / "load_engine.cnf"),
            "proxy.config.ssl.async.handshake.enabled": 1,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")
    return ats, origin


def test_tls_engine(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The asynchronous engine signals completion and serves a TLS request.

    :param ats_factory: Factory for the TLS proxy.
    :param services: Factory for the HTTP origin.
    :param curl: Transport-aware request runner.
    """
    ats, origin = configure_engine(ats_factory, services)
    origin.start()
    ats.start()
    result = curl.run_for(
        ats, f"--insecure --verbose --header uuid:basic --header host:example.com https://127.0.0.1:{ats.https_port}/")
    assert result.returncode == 0, result.output
    assert re.search(r"HTTP/(2|1\.1) 200", result.stderr), result.output
    assert result.stdout == "ok"
    wait_for_file_lines(ats.traffic_out, "Send signal to ", 1)
