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

from tools.uranium.services import ATSFactory, Curl, ServiceFactory, wait_for_file_lines


@pytest.mark.parametrize("delayed_hooks", [1, 2], ids=["one-delayed-hook", "two-delayed-hooks"])
def test_client_hello_read_after_delayed_hook(
        ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, delayed_hooks: int) -> None:
    """A ClientHello hook after an asynchronously reenabled one can still read the ClientHello.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param delayed_hooks: Number of ClientHello hooks that reenable the connection asynchronously.
    """

    origin = services.verifier_server("origin", "client_hello_reader.replay.yaml", http_ports=[])
    ats = ats_factory.create("ts", enable_tls=True)
    if not ats.plugin_exists("jax_fingerprint.so"):
        pytest.skip("jax_fingerprint.so is required")
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.show_location": 0,
            "proxy.config.diags.debug.tags": "ssl_hook_test|jax_fingerprint",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.remap_config.add_line(f"map https://example.com:{ats.https_port} https://127.0.0.1:{origin.https_port}")
    ats.copy_custom_plugin("{AtsTestPluginsDir}/ssl_hook_test.so")
    # The delayed hooks come first, so the fingerprint hook runs after the handshake resumes.
    ats.plugin_config.add_line(f"ssl_hook_test.so -client_hello={delayed_hooks}")
    ats.plugin_config.add_line("jax_fingerprint.so --method JA4 --header x-jax --log-filename jax_fingerprint --standalone")

    origin.start()
    ats.start()
    result = curl.run_for(
        ats,
        (
            f"--verbose --insecure --http1.1 --header 'host:example.com:{ats.https_port}' "
            "--header 'uuid: client-hello-reader' "
            f"'https://127.0.0.1:{ats.https_port}/'"),
        timeout=15,
    )
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200" in result.stderr, result.output

    content = wait_for_file_lines(ats.log_directory / "jax_fingerprint.log", "JA4: ", 1)
    assert re.search(r"JA4: [a-z0-9]{10}_[0-9a-f]{12}_[0-9a-f]{12}$", content, re.MULTILINE), content
    traffic_out = ats.traffic_out.read_text(errors="replace")
    for hook in range(delayed_hooks):
        assert f"Client Hello callback {hook} " in traffic_out, traffic_out
