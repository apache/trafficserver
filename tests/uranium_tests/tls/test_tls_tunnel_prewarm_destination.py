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

import time

from tools.uranium.services import ATS, ATSFactory, wait_for_file_lines

WARNING = "not pre-warming tunnel destination"

STARTUP_SNI = (
    "sni:\n"
    "  - fqdn: template.example.com\n"
    "    forward_route: 127.0.0.1:{inbound_local_port}\n"
    "    tunnel_prewarm: true\n"
    "  - fqdn: empty.example.com\n"
    '    partial_blind_route: "127.0.0.1:"\n'
    "    tunnel_prewarm: true\n"
)

RELOAD_SNI = "sni:\n  - fqdn: pp.example.com\n    forward_route: 127.0.0.1:{proxy_protocol_port}\n    tunnel_prewarm: true\n"


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure pre-warmed tunnel routes whose port is not a fixed number.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.remap_config.add_line("map / http://127.0.0.1:1/")
    ats.write_config_file("sni.yaml", STARTUP_SNI)
    return ats


def test_tls_tunnel_prewarm_destination(ats_factory: ATSFactory) -> None:
    """A pre-warm destination without a fixed port is skipped, at startup and on reload.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    diags = wait_for_file_lines(_ats.diags_log, WARNING, 2)
    assert '"127.0.0.1:{inbound_local_port}"' in diags
    assert '"127.0.0.1:"' in diags
    assert _ats.is_running

    # Give the rewritten file a newer mtime than the loaded one.
    time.sleep(2)
    (_ats.config_directory / "sni.yaml").write_text(RELOAD_SNI)
    result = _ats.traffic_ctl("config", "reload", "-m", "-t", "prewarm-destination", "-w", "1", "-r", "0.5", "-T", "30s")
    assert result.returncode == 0, result.output
    diags = wait_for_file_lines(_ats.diags_log, WARNING, 3)
    assert '"127.0.0.1:{proxy_protocol_port}"' in diags
    assert _ats.is_running
