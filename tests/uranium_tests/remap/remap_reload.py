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
import time

from tools.uranium.services import ATS, ATSFactory, DNSServer, ServiceFactory, VerifierServer


class RemapReloadScenario:
    """Keep an old remap after a failed reload, then install a valid update."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, *, use_yaml: bool) -> None:
        self._services = services
        self._use_yaml = use_yaml
        self._origin = self.configure_origin(services)
        self._dns = self.configure_dns(services)
        self._ats = self.configure_ats(ats_factory)
        self._client_index = 0

    def configure_origin(self, services: ServiceFactory) -> VerifierServer:
        """Create the shared origin used before and after reloads."""

        return services.verifier_server("origin", "reload_server.replay.yaml")

    def configure_dns(self, services: ServiceFactory) -> DNSServer:
        """Resolve every synthetic remap hostname locally."""

        return services.dns("dns", default="127.0.0.1")

    def classic_rules(self, hosts: tuple[str, ...]) -> list[str]:
        """Render classic remap rules for @a hosts."""

        return [f"map http://{host}.ex http://{host}.ex:{self._origin.http_port}" for host in hosts]

    def yaml_rules(self, hosts: tuple[str, ...]) -> list[str]:
        """Render YAML remap rules for @a hosts."""

        lines = ["remap:"]
        for host in hosts:
            lines.extend(
                [
                    "  - type: map",
                    f"    from: {{url: 'http://{host}.ex'}}",
                    f"    to: {{url: 'http://{host}.ex:{self._origin.http_port}'}}",
                ])
        return lines

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure four valid initial rules and a three-rule minimum."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.url_remap.min_rules_required": 3,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "remap|config|file|rpc",
            })
        hosts = ("alpha", "bravo", "charlie", "delta")
        (ats.remap_yaml if self._use_yaml else
         ats.remap_config).add_lines(self.yaml_rules(hosts) if self._use_yaml else self.classic_rules(hosts))
        return ats

    @property
    def config_path(self) -> Path:
        """Return the active remap configuration path."""

        return self._ats.config_directory / ("remap.yaml" if self._use_yaml else "remap.config")

    def write_rules(self, hosts: tuple[str, ...]) -> None:
        """Replace the active remap file without triggering a reload implicitly."""

        lines = self.yaml_rules(hosts) if self._use_yaml else self.classic_rules(hosts)
        self.config_path.write_text("\n".join(lines) + "\n")

    def reload(self, token: str, expected: str) -> None:
        """Schedule a reload and wait for its terminal status."""

        result = self._ats.traffic_ctl("config", "reload", "--token", token)
        assert result.returncode == 0, result.output
        deadline = time.monotonic() + 15
        latest = ""
        while time.monotonic() < deadline:
            status = self._ats.traffic_ctl("config", "status", "--token", token)
            latest = status.output.lower()
            if expected in latest:
                return
            if (expected == "success" and "failed" in latest) or (expected == "failed" and "success" in latest):
                break
            time.sleep(0.1)
        raise AssertionError(f"Reload {token!r} did not become {expected}:\n{latest}")

    def run_client(self, replay: str) -> None:
        """Run one verifier client against the current remap generation."""

        self._client_index += 1
        self._services.verifier_client(
            f"client-{self._client_index}",
            replay,
            http_ports=[self._ats.http_port],
        ).run()

    def run(self) -> None:
        """Exercise initial, rejected, and accepted remap generations."""

        self._origin.start()
        self._dns.start()
        self._ats.start()
        self.run_client("reload_1.replay.yaml")

        self.write_rules(("alpha", "bravo"))
        self.reload("too-few-rules", "failed")
        self.run_client("reload_2.replay.yaml")

        self.write_rules(("echo", "foxtrot", "golf", "hotel", "india"))
        self.reload("enough-rules", "success")
        self.run_client("reload_3.replay.yaml")
        self.run_client("reload_4.replay.yaml")
