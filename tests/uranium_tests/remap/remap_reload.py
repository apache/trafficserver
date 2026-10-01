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


def run_remap_reload(ats_factory: ATSFactory, services: ServiceFactory, *, use_yaml: bool) -> None:
    """Keep an old remap after a failed reload, then install a valid update.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param use_yaml: Use yaml used by this test step.
    """

    def configure_origin(services: ServiceFactory) -> VerifierServer:
        """Create the shared origin used before and after reloads.

        :param services: Factory owning support services and their cleanup.
        """

        return services.verifier_server("origin", "reload_server.replay.yaml")

    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Resolve every synthetic remap hostname locally.

        :param services: Factory owning support services and their cleanup.
        """

        return services.dns("dns", default="127.0.0.1")

    def classic_rules(hosts: tuple[str, ...]) -> list[str]:
        """Render classic remap rules for @a hosts.

        :param hosts: Hosts used by this test step.
        """

        return [f"map http://{host}.ex http://{host}.ex:{_origin.http_port}" for host in hosts]

    def yaml_rules(hosts: tuple[str, ...]) -> list[str]:
        """Render YAML remap rules for @a hosts.

        :param hosts: Hosts used by this test step.
        """

        lines = ["remap:"]
        for host in hosts:
            lines.extend(
                [
                    "  - type: map",
                    f"    from: {{url: 'http://{host}.ex'}}",
                    f"    to: {{url: 'http://{host}.ex:{_origin.http_port}'}}",
                ])
        return lines

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure four valid initial rules and a three-rule minimum.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.url_remap.min_rules_required": 3,
                "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "remap|config|file|rpc",
            })
        hosts = ("alpha", "bravo", "charlie", "delta")
        (ats.remap_yaml if use_yaml else ats.remap_config).add_lines(yaml_rules(hosts) if use_yaml else classic_rules(hosts))
        return ats

    def config_path() -> Path:
        """Return the active remap configuration path."""

        return _ats.config_directory / ("remap.yaml" if use_yaml else "remap.config")

    def write_rules(hosts: tuple[str, ...]) -> None:
        """Replace the active remap file without triggering a reload implicitly.

        :param hosts: Hosts used by this test step.
        """

        lines = yaml_rules(hosts) if use_yaml else classic_rules(hosts)
        config_path().write_text("\n".join(lines) + "\n")

    def reload(token: str, expected: str) -> None:
        """Schedule a reload and wait for its terminal status.

        :param token: Token used by this test step.
        :param expected: Expected result for this case.
        """

        result = _ats.traffic_ctl("config", "reload", "--token", token)
        assert result.returncode == 0, result.output
        deadline = time.monotonic() + 15
        latest = ""
        while time.monotonic() < deadline:
            status = _ats.traffic_ctl("config", "status", "--token", token)
            latest = status.output.lower()
            if expected in latest:
                return
            if (expected == "success" and "failed" in latest) or (expected == "failed" and "success" in latest):
                break
            time.sleep(0.1)
        raise AssertionError(f"Reload {token!r} did not become {expected}:\n{latest}")

    def run_client(replay: str) -> None:
        """Run one verifier client against the current remap generation.

        :param replay: Replay used by this test step.
        """
        nonlocal _client_index

        _client_index += 1
        _services.verifier_client(
            f"client-{_client_index}",
            replay,
            http_ports=[_ats.http_port],
        ).run()

    _services = services
    _origin = configure_origin(services)
    _dns = configure_dns(services)
    _ats = configure_ats(ats_factory)
    _client_index = 0

    _origin.start()
    _dns.start()
    _ats.start()
    run_client("reload_1.replay.yaml")

    write_rules(("alpha", "bravo"))
    reload("too-few-rules", "failed")
    run_client("reload_2.replay.yaml")

    write_rules(("echo", "foxtrot", "golf", "hotel", "india"))
    reload("enough-rules", "success")
    run_client("reload_3.replay.yaml")
    run_client("reload_4.replay.yaml")
