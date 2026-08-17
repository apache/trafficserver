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

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory, VerifierServer


class AllowPlainScenario:
    """Serve TLS and clear-text HTTP on one `ssl:allow-plain` listener."""

    _replay = "replay/allow-plain.replay.yaml"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)

    def configure_server(self, services: ServiceFactory) -> VerifierServer:
        """Create the GET and POST verifier origin."""

        return services.verifier_server("server", self._replay, https_ports=[])

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure the TLS listener to recognize clear-text requests."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.http.server_ports": f"{ats.https_port}:ssl:allow-plain",
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "ssl|http",
            })
        ats.remap_config.add_lines(
            (
                f"map / http://127.0.0.1:{self._server.http_port}",
                f"map /post http://127.0.0.1:{self._server.http_port}/post",
            ))
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        return ats

    def run(self) -> None:
        """Verify TLS, plain GET, and repeated large plain POST requests."""

        self._server.start()
        self._ats.start()
        resolve = f"www.example.com:{self._ats.https_port}:127.0.0.1"
        tls = self._curl.run_for(
            self._ats,
            (
                f"--output /dev/null --insecure --verbose --header 'uuid: get' --ipv4 --http1.1 --resolve '{resolve}' "
                f"'https://www.example.com:{self._ats.https_port}/'"),
        )
        assert tls.returncode == 0, tls.output
        assert "TLS" in tls.stderr

        plain = self._curl.run_for(
            self._ats,
            (
                f"--verbose --ipv4 --http1.1 --header 'uuid: get' --resolve '{resolve}' "
                f"'http://www.example.com:{self._ats.https_port}'"),
        )
        assert plain.returncode == 0, plain.output
        assert "TLS" not in plain.stderr

        body = self._ats.run_directory / "big_post_body"
        body.write_text("0123456789" * 50000)
        post = self._curl.run_for(
            self._ats,
            (
                f"--verbose --data '@{body}' --header 'uuid: post' --ipv4 --http1.1 --resolve '{resolve}' "
                f"'http://www.example.com:{self._ats.https_port}/post' "
                f"'http://www.example.com:{self._ats.https_port}/post'"),
        )
        assert post.returncode == 0, post.output
        assert "TLS" not in post.stderr
        assert post.stderr.count("HTTP/1.1 200") == 2


def test_allow_plain(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An allow-plain TLS port accepts encrypted and clear-text clients."""

    AllowPlainScenario(ats_factory, services, curl).run()
