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

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory, VerifierServer, wait_for_file_lines

LOG_FORMAT = "path=%<cqup> crc=%<crc> ja3=%<jaxja3> ja4=%<jaxja4> ja4h=%<jaxja4h>"


def configure_ats(ats_factory: ATSFactory, origin: VerifierServer, dns_port: int, *, enable_quic: bool = False) -> ATS:
    """Create a TLS proxy that logs all three fingerprints through --log-field.

    :param ats_factory: Factory owning the TLS proxy.
    :param origin: TLS origin that jax.server.test maps to.
    :param dns_port: Port of the DNS server that resolves jax.backend.test.
    :param enable_quic: Whether ATS also accepts HTTP/3 on its TLS port.
    """
    ats = ats_factory.create("ts", enable_cache=False, enable_tls=True, enable_quic=enable_quic)
    if not ats.plugin_exists("jax_fingerprint.so"):
        pytest.skip("jax_fingerprint.so is required")
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.dns.nameservers": f"127.0.0.1:{dns_port}",
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
                    "formats": [{
                        "name": "jax_custom",
                        "format": LOG_FORMAT
                    }],
                    "logs": [{
                        "filename": "jax_log_field",
                        "format": "jax_custom"
                    }],
                }
        })
    return ats


def check_clean_shutdown(ats: ATS) -> str:
    """Stop ATS and check that it logged no errors and freed every session copy it made.

    :param ats: The proxy, still running.
    :return: The traffic.out content.
    """
    ats.stop()
    traffic = ats.traffic_out.read_text(errors="replace")
    assert "failed assertion" not in traffic
    created = traffic.count("Created --log-field session copies")
    freed = traffic.count("Freed --log-field session copies")
    assert created == freed, f"created {created} --log-field session copies but freed {freed}"
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert not re.search(r"ERROR:|FATAL:|Unrecognized configuration value", diagnostics), diagnostics
    return traffic


def test_jax_fingerprint_log_field(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Retain connection fingerprints through close and incomplete-request logging.

    :param ats_factory: Factory owning the TLS proxy.
    :param services: Factory owning DNS, verifier, and partial TLS clients.
    """
    dns = services.dns("dns", default="127.0.0.1")
    replay = "jax_fingerprint_log_field.replay.yaml"
    origin = services.verifier_server("origin", replay)
    ats = configure_ats(ats_factory, origin, dns.port)
    dns.start()
    origin.start()
    ats.start()
    client = services.verifier_client("client", replay, http_ports=[ats.http_port], https_ports=[ats.https_port])
    assert client.run().returncode == 0
    # The partial client stalls in its request header, so ATS logs it with no JA4H. The keep-alive client first
    # completes a request on the same session, whose JA4H must not be logged for the stalled one. The
    # handshake-only client never sends a request, so ATS logs nothing for it; it checks that ATS survives a
    # session with no transaction.
    for name, arguments in (
        ("partial", ["--path", "/partial"]),
        ("keep-alive", ["--complete-first", "keep-alive-first", "--path", "/keep-alive-partial"]),
        ("handshake-only", []),
    ):
        helper = services.process(
            name,
            [sys.executable,
             services.resolve_path("tls_partial_client.py"),
             str(ats.https_port), "jax.server.test", *arguments])
        helper.stdout.contains("ATS closed the connection", "ATS must close the stalled connection.")
        result = helper.run()
        assert result.returncode == 0, result.output
        assert ats.is_running
    log = ats.log_directory / "jax_log_field.log"
    fingerprints = r"ja3=[0-9a-f]{32} ja4=t13[0-9a-z_]+"
    wait_for_file_lines(log, rf"path=connection-close crc=\S+ {fingerprints} ja4h=ge[0-9a-z_]{{9,}}", 1)
    wait_for_file_lines(log, rf"path=partial crc=ERR_CLIENT_ABORT {fingerprints} ja4h=-$", 1)
    # Both the verifier client and the keep-alive helper request keep-alive-first.
    wait_for_file_lines(log, rf"path=keep-alive-first crc=\S+ {fingerprints} ja4h=ge[0-9a-z_]{{9,}}", 2)
    wait_for_file_lines(log, rf"path=keep-alive-partial crc=ERR_CLIENT_ABORT {fingerprints} ja4h=-$", 1)
    traffic = check_clean_shutdown(ats)
    assert not re.search(r"ja[34]=-", log.read_text(errors="replace"))
    assert "Created --log-field session copies" in traffic


def test_jax_fingerprint_log_field_http3(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Log the fields for HTTP/3 requests, including one the client gives up on, without crashing or leaking.

    The JA3 and JA4 instances hook SSN_START, which HTTP/3 sessions dispatch too. HTTP/3 sessions never
    dispatch SSN_CLOSE, so the plugin keeps no session copies for them and JA4H is logged as ``-``. QUIC does
    not run the client hello hook, so JA3 and JA4 are ``-`` as well.

    :param ats_factory: Factory owning the QUIC proxy.
    :param services: Factory owning DNS and the origin.
    :param curl: Transport-aware curl command runner.
    """
    if not ats_factory.has_feature("TS_USE_QUIC") or not Curl.supports("http3"):
        pytest.skip("ATS QUIC and curl HTTP/3 support are required")
    if curl.uses_uds:
        pytest.skip("HTTP/3 requires a UDP listener")
    dns = services.dns("dns", default="127.0.0.1")
    origin = services.verifier_server("origin", "jax_fingerprint_log_field_http3.replay.yaml")
    ats = configure_ats(ats_factory, origin, dns.port, enable_quic=True)
    ats.set_startup_timeout(60)
    ats.records.update({"proxy.config.quic.server.stateless_retry_enabled": 0})
    # curl's :authority carries the port.
    ats.remap_config.add_line(f"map https://jax.server.test:{ats.https_port} https://jax.backend.test:{origin.https_port}")
    dns.start()
    origin.start()
    ats.start()
    # --next keeps the connection, so all three requests share one HTTP/3 session.
    url = f"https://jax.server.test:{ats.https_port}"
    common = f"--silent --http3-only --insecure --resolve 'jax.server.test:{ats.https_port}:127.0.0.1'"
    result = curl.run(
        f"{common} --header 'uuid: h3-first' {url}/h3-first --next "
        f"{common} --header 'uuid: h3-second' {url}/h3-second --next "
        f"{common} --header 'uuid: h3-slow' --max-time 1 {url}/h3-slow",
        timeout=30)
    assert result.returncode == 28, result.output
    log = ats.log_directory / "jax_log_field.log"
    for path in ("h3-first", "h3-second", "h3-slow"):
        wait_for_file_lines(log, rf"path={path} crc=\S+ ja3=- ja4=- ja4h=-$", 1)
    assert ats.is_running
    traffic = check_clean_shutdown(ats)
    assert "Not saving --log-field session copies on HQ session" in traffic
    assert "Created --log-field session copies" not in traffic
