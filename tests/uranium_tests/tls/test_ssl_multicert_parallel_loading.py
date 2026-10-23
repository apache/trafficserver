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

from tools.uranium.services import ATSFactory, Curl, ServiceFactory, wait_for_file_lines


def test_ssl_multicert_parallel_loading(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Serve the correct SNI certificates despite an invalid parallel load.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning the origin and its cleanup.
    :param curl: Transport-aware client used to inspect certificate subjects.
    """
    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: foo.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    ats = ats_factory.create("ats", enable_tls=True)
    ats.records.update(
        {
            "proxy.config.ssl.server.multicert.concurrency": 4,
            "proxy.config.ssl.server.multicert.exit_on_load_fail": 0,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "ssl_load",
        })
    for stem in ("signed-foo", "signed-bar", "signed-san", "signed-wild"):
        ats.copy_to_ssl(f"ssl/{stem}.pem", f"ssl/{stem}.key")
        ats.ssl_multicert_config.add_line(f"ssl_cert_name={stem}.pem ssl_key_name={stem}.key")
    ats.copy_to_ssl("ssl/combo.pem")
    ats.ssl_multicert_config.add_lines(
        (
            "dest_ip=* ssl_cert_name=combo.pem",
            "ssl_cert_name=nonexistent.pem ssl_key_name=nonexistent.key",
        ))
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.http_port}")
    origin.start()
    ats.start()
    for domain, subject in (
        ("foo.com", "foo.com"),
        ("bar.com", "bar.com"),
        ("group.com", "group.com"),
        ("unknown.example.com", "random.server.com"),
    ):
        result = curl.run_for(
            ats,
            f"--silent --verbose --insecure --resolve '{domain}:{ats.https_port}:127.0.0.1' "
            f"'https://{domain}:{ats.https_port}/'",
        )
        assert result.returncode == 0, result.output
        assert "Could Not Connect" not in result.stdout
        assert f"CN={subject}" in result.stderr, result.stderr
    wait_for_file_lines(ats.diags_log, "failed to load certificate", 1)
