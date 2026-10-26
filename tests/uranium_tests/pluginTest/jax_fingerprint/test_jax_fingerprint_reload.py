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

import pytest
import yaml

from tools.uranium.services import ATS, ATSFactory, ServiceFactory, wait_for_file_lines


def configure_proxy(ats_factory: ATSFactory, services: ServiceFactory, replay: str) -> ATS:
    """Start dependencies and configure an unstarted TLS proxy.

    :param ats_factory: Factory owning the proxy.
    :param services: Factory owning DNS and verifier origins.
    :param replay: Replay filename relative to this module.
    """
    ats = ats_factory.create("ts", enable_cache=False, enable_tls=True)
    if not ats.plugin_exists("jax_fingerprint.so"):
        pytest.skip("jax_fingerprint.so is required")
    dns = services.dns("dns", default="127.0.0.1")
    origin = services.verifier_server("origin", replay)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.proxy_name": "test.proxy.test",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "jax_fingerprint",
        })
    for host in ("jax.server.test", "jax-filtered.server.test"):
        ats.remap_config.add_line(f"map https://{host} https://jax.backend.test:{origin.https_port}")
    if replay == "jax_fingerprint_remap_reload.replay.yaml":
        ats.remap_config.add_line(
            f"map http://jax.server.test http://jax.backend.test:{origin.http_port} "
            "@plugin=jax_fingerprint.so @pparam=jax_fingerprint_remap.yaml")
        ats.write_config_file("jax_fingerprint_remap.yaml", config_text({"method": "JA4H", "header": "x-jax-initial"}))
    dns.start()
    origin.start()
    return ats


def config_text(fingerprint: dict[str, object]) -> str:
    """Serialize one fingerprint configuration.

    :param fingerprint: YAML fingerprint settings.
    """
    return yaml.safe_dump({"jax_fingerprint": {"fingerprints": [fingerprint]}})


def run_client(ats: ATS, services: ServiceFactory, replay: str, name: str, keys: str) -> None:
    """Send selected requests and verify all replay assertions.

    :param ats: Running proxy to target.
    :param services: Factory owning verifier clients.
    :param replay: Replay filename.
    :param name: Unique client process name.
    :param keys: Space-separated replay transaction keys.
    """
    client = services.verifier_client(name, replay, http_ports=[ats.http_port], https_ports=[ats.https_port], keys=keys)
    result = client.run()
    assert result.returncode == 0, result.output


def test_jax_fingerprint_reload(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Reload each plugin instance independently and preserve state on rejection.

    :param ats_factory: Factory owning the proxy.
    :param services: Factory owning DNS and verifier processes.
    """
    replay = "jax_fingerprint_reload.replay.yaml"
    ats = configure_proxy(ats_factory, services, replay)
    fingerprint = {
        "method": "JA4",
        "standalone": True,
        "header": "x-jax",
        "via_header": "x-jax-via",
        "servernames": ["jax.server.test"]
    }
    ats.write_config_file("jax_fingerprint.yaml", config_text(fingerprint))
    ats.write_config_file("jax_fingerprint_ja4h.yaml", config_text({"method": "JA4H", "standalone": True, "header": "x-ja4h"}))
    ats.plugin_config.add_lines(["jax_fingerprint.so jax_fingerprint.yaml", "jax_fingerprint.so jax_fingerprint_ja4h.yaml"])
    rejected = r"Configuration reload from jax_fingerprint\.yaml rejected: 'method' of fingerprint entry 1 changed"
    ats.start()
    run_client(ats, services, replay, "initial", "initial-allowed initial-filtered")
    fingerprint["servernames"] = ["jax-filtered.server.test"]
    (ats.config_directory / "jax_fingerprint.yaml").write_text(config_text(fingerprint))
    result = ats.traffic_ctl("plugin", "msg", "jax_fingerprint.reload")
    assert result.returncode == 0, result.output
    wait_for_file_lines(ats.diags_log, r"Configuration reloaded successfully from jax_fingerprint\.yaml", 1)
    wait_for_file_lines(ats.diags_log, r"Configuration reloaded successfully from jax_fingerprint_ja4h\.yaml", 1)
    run_client(ats, services, replay, "reloaded", "reloaded-allowed reloaded-filtered")
    fingerprint.update(method="JA3", servernames=["jax.server.test"])
    (ats.config_directory / "jax_fingerprint.yaml").write_text(config_text(fingerprint))
    result = ats.traffic_ctl("plugin", "msg", "jax_fingerprint.reload")
    assert result.returncode == 0, result.output
    wait_for_file_lines(ats.diags_log, rejected, 1)
    wait_for_file_lines(ats.diags_log, r"Configuration reloaded successfully from jax_fingerprint_ja4h\.yaml", 2)
    run_client(ats, services, replay, "rejected", "reloaded-allowed reloaded-filtered")
    ats.stop()
    for line in ats.diags_log.read_text(errors="replace").splitlines():
        assert "FATAL:" not in line and "Unrecognized configuration value" not in line, line
        assert "ERROR:" not in line or re.search(rejected, line), line


def test_jax_fingerprint_remap_reload(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Reload a remap fingerprint by editing only its referenced YAML file.

    :param ats_factory: Factory owning the proxy.
    :param services: Factory owning DNS and verifier processes.
    """
    replay = "jax_fingerprint_remap_reload.replay.yaml"
    ats = configure_proxy(ats_factory, services, replay)
    ats.start()
    run_client(ats, services, replay, "initial", "remap-initial")
    path = ats.config_directory / "jax_fingerprint_remap.yaml"
    path.write_text(config_text({"method": "JA4H", "header": "x-jax-reloaded"}))
    result = ats.traffic_ctl("config", "reload")
    assert result.returncode == 0, result.output
    wait_for_file_lines(ats.traffic_out, re.escape(f"Loading configuration from {path}"), 2, timeout=60)
    run_client(ats, services, replay, "reloaded", "remap-reloaded")
    ats.stop()
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert not re.search(r"ERROR:|FATAL:|Unrecognized configuration value", diagnostics), diagnostics
