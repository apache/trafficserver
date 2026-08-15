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

from tools.uranium.services import ATSFactory, Curl, ServiceFactory, assert_matches_gold


class ProxyServeStaleDnsFailScenario:
    """A child and parent proxy serve stale content after DNS failure."""

    SERVER_NAME = "http://unknown.domain.com/"

    def __init__(self, ats_factory: ATSFactory, curl: Curl, services: ServiceFactory) -> None:
        self.ats_factory = ats_factory
        self.curl = curl
        self.services = services

    def _configure_dns(self) -> None:
        self.dns = self.services.dns("dns")
        self.dns.add_records({"localhost": ["127.0.0.1"]})

    def _configure_traffic_servers(self) -> None:
        self.child = self.ats_factory.create("ts_child")
        self.parent = self.ats_factory.create("ts_parent", enable_uds=False)
        self._configure_child_proxy()
        self._configure_parent_proxy()

    def _configure_child_proxy(self) -> None:
        self.child.records.update(
            {
                "proxy.config.http.push_method_enabled": 1,
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.http.cache.max_stale_age": 10,
                "proxy.config.http.parent_proxy.self_detect": 0,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self.dns.port}",
            })
        self.child.parent_config.add_line(
            f"dest_domain=. parent=localhost:{self.parent.http_port} round_robin=consistent_hash go_direct=false")
        self.child.remap_config.add_line(f"map http://localhost:{self.child.http_port} {self.SERVER_NAME}")

    def _configure_parent_proxy(self) -> None:
        self.parent.records.update(
            {
                "proxy.config.http.push_method_enabled": 1,
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.http.cache.max_stale_age": 10,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self.dns.port}",
            })
        self.parent.remap_config.add_lines(
            [
                f"map http://localhost:{self.parent.http_port} {self.SERVER_NAME}",
                f"map {self.SERVER_NAME} {self.SERVER_NAME}",
            ])

    def _start_services(self) -> None:
        self.dns.start()
        self.parent.start()
        self.child.start()

    def _exercise_stale_cache_behavior(self) -> None:
        stale_5 = (
            "HTTP/1.1 200 OK\nServer: ATS/10.0.0\nAccept-Ranges: bytes\nContent-Length: 6\n"
            "Cache-Control: public, max-age=5\n\nCACHED")
        stale_10 = stale_5.replace("max-age=5", "max-age=10")
        script = (
            f'{{curl}} -X PUSH -d "{stale_5}" "http://localhost:{self.child.http_port}";'
            f'{{curl}} -X PUSH -d "{stale_10}" "http://localhost:{self.parent.http_port}";'
            f"sleep 7; {{curl}} -s -v http://localhost:{self.child.http_port};"
            f"sleep 17; {{curl}} -s -v http://localhost:{self.child.http_port};"
            f'{{curl_base}} -X PUSH -d "{stale_5}" "http://localhost:{self.parent.http_port}";'
            f"sleep 7; {{curl_base}} -s -v http://localhost:{self.parent.http_port};"
            f"sleep 17; {{curl_base}} -s -v http://localhost:{self.parent.http_port};")
        result = self.curl.run_script(self.child, script, timeout=70)

        assert result.returncode == 0, result.output
        assert_matches_gold(result.stderr, Path(__file__).parent / "gold/serve_stale_dns_fail.gold")

    def run(self) -> None:
        self._configure_dns()
        self._configure_traffic_servers()
        self._start_services()
        self._exercise_stale_cache_behavior()


def test_proxy_serve_stale_dns_fail(ats_factory: ATSFactory, curl: Curl, services: ServiceFactory) -> None:
    ProxyServeStaleDnsFailScenario(ats_factory, curl, services).run()
