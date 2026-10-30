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
import sys

import pytest

from tools.uranium.services import ATSFactory, ServiceFactory, wait_for_file_lines


def test_jax_fingerprint_log_field(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Retain connection fingerprints through close and incomplete-request logging.

    :param ats_factory: Factory owning the TLS proxy.
    :param services: Factory owning DNS, verifier, and partial TLS clients.
    """
    ats = ats_factory.create("ts", enable_cache=False, enable_tls=True)
    if not ats.plugin_exists("jax_fingerprint.so"):
        pytest.skip("jax_fingerprint.so is required")
    dns = services.dns("dns", default="127.0.0.1")
    replay = "jax_fingerprint_log_field.replay.yaml"
    origin = services.verifier_server("origin", replay)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "jax_fingerprint|http",
            "proxy.config.log.max_secs_per_buffer": 1,
            "proxy.config.http.transaction_no_activity_timeout_in": 2,
            "proxy.config.http.accept_no_activity_timeout": 2,
        })
    for method in ("JA3", "JA4", "JA4H"):
        ats.plugin_config.add_line(
            f"jax_fingerprint.so --method {method} --header x-{method.lower()} --log-field jax{method.lower()} --standalone")
    ats.remap_config.add_line(f"map https://jax.server.test https://jax.backend.test:{origin.https_port}")
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats":
                        [{
                            "name": "jax_custom",
                            "format": "path=%<cqup> crc=%<crc> ja3=%<jaxja3> ja4=%<jaxja4> ja4h=%<jaxja4h>"
                        }],
                    "logs": [{
                        "filename": "jax_log_field",
                        "format": "jax_custom"
                    }],
                }
        })
    dns.start()
    origin.start()
    ats.start()
    client = services.verifier_client("client", replay, http_ports=[ats.http_port], https_ports=[ats.https_port])
    assert client.run().returncode == 0
    for name, arguments in (("partial", ["--path", "/partial"]), ("handshake-only", [])):
        helper = services.process(
            name,
            [sys.executable,
             services.resolve_path("tls_partial_client.py"),
             str(ats.https_port), "jax.server.test", *arguments])
        helper.stdout.contains("ATS closed the connection", "ATS must time out and close the stalled connection.")
        result = helper.run()
        assert result.returncode == 0, result.output
        assert ats.is_running
    log = ats.log_directory / "jax_log_field.log"
    fingerprints = r"ja3=[0-9a-f]{32} ja4=t13[0-9a-z_]+"
    wait_for_file_lines(log, rf"path=connection-close crc=\S+ {fingerprints} ja4h=ge[0-9a-z_]{{9,}}", 1)
    wait_for_file_lines(log, rf"path=partial crc=ERR_CLIENT_ABORT {fingerprints}", 1)
    ats.stop()
    assert not re.search(r"ja[34]=-", log.read_text(errors="replace"))
    assert "failed assertion" not in ats.traffic_out.read_text(errors="replace")
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert not re.search(r"ERROR:|FATAL:|Unrecognized configuration value", diagnostics), diagnostics
