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
"""Per-remap bypass_parent applies to CONNECT parenting."""
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory


def test_bypass_parent_connect(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Bypass a dead parent only for the rule opting into direct connections.

    :param ats_factory: Proxy factory.
    :param services: DNS and verifier factory.
    """
    replay = Path(__file__).parent / "replays/bypass_parent_connect.replay.yaml"
    origin = services.verifier_server("origin", replay)
    origin.stdout.excludes("uuid: connect-", "Validate the origin's framing and request handling")
    dns = services.dns("dns", default="127.0.0.1")
    ats = ats_factory.create("ts", enable_cache=False)
    if not ats.plugin_exists("conf_remap.so"):
        pytest.skip("conf_remap.so is required")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http_trans|parent_select",
            "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http.connect_ports": str(origin.http_port),
            "proxy.config.http.parent_proxy.self_detect": 0,
            "proxy.config.http.uncacheable_requests_bypass_parent": 0,
            "proxy.config.http.parent_proxy.total_connect_attempts": 1,
            "proxy.config.http.parent_proxy.per_parent_connect_attempts": 1,
        })
    ats.parent_config.add_line('dest_domain=. parent="127.0.0.1:1|1" go_direct=false parent_is_proxy=true')
    ats.remap_config.add_lines(
        [
            f"map http://connect-direct.test:80/ http://origin.test:{origin.http_port}/ "
            "@plugin=conf_remap.so @pparam=proxy.config.http.bypass_parent=1",
            f"map http://connect-parent.test:80/ http://origin.test:{origin.http_port}/",
        ])
    ats.allow_private_connect()
    dns.start()
    origin.start()
    ats.start()
    result = services.verifier_client("client", replay, http_ports=[ats.http_port]).run()
    assert result.returncode == 0, result.output
    assert ats.is_running and origin.is_running and dns.is_running
