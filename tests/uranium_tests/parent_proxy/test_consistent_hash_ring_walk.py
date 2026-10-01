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

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, ServiceFactory, wait_for_file_lines

NUM_PARENTS = 100


def configure_ats(ats_factory: ATSFactory, *, _hostnames: list[str], _ports: list[int]) -> ATS:
    """Configure the parent pool and parent-selection diagnostics.

    :param _hostnames: Test-local hostnames configured by the test.
    :param _ports: Test-local ports configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "parent_select",
            "proxy.config.http.no_dns_just_forward_to_parent": 1,
            "proxy.config.http.parent_proxy.fail_threshold": 10,
            "proxy.config.http.parent_proxy.retry_time": 300,
            "proxy.config.http.parent_proxy.self_detect": 0,
            "proxy.config.url_remap.remap_required": 0,
        })
    parent_list = ", ".join(f"{hostname}:{port}|1" for hostname, port in zip(_hostnames, _ports, strict=True))
    ats.parent_config.add_line(
        f'dest_domain=. parent="{parent_list}" round_robin=consistent_hash go_direct=false parent_is_proxy=true')
    return ats


def mark_parents_down(*, _ats: ATS, _hostnames: list[str]) -> None:
    """Populate HostStatus for the complete pool in one RPC call.

    :param _ats: Test-local ats configured by the test.
    :param _hostnames: Test-local hostnames configured by the test.
    """

    result = _ats.traffic_ctl("host", "down", *_hostnames)
    assert result.returncode == 0, result.output


def verify_response(result: CommandResult) -> None:
    """Require the all-down pool to generate a 502 response.

    :param result: Completed command result to validate.
    """

    assert result.returncode == 0, result.output
    assert result.stdout == "502"


def test_consistent_hash_ring_walk(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An all-down hash pool reads each distinct parent's HostStatus once.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _hostnames = [f"deadparent{index:03d}" for index in range(1, NUM_PARENTS + 1)]
    _ports = [services.allocate_port() for _ in _hostnames]
    _ats = configure_ats(ats_factory, _hostnames=_hostnames, _ports=_ports)

    _ats.start()
    mark_parents_down(_ats=_ats, _hostnames=_hostnames)
    result = curl.run_for(
        _ats,
        (
            f"--silent --output /dev/null --write-out '%{{http_code}}' --proxy '127.0.0.1:{_ats.http_port}' "
            f"http://example.com/ring-walk-probe"),
    )
    verify_response(result)
    traffic_out = wait_for_file_lines(_ats.traffic_out, rf"getHostStatus calls: {NUM_PARENTS}\b", 1)
    assert re.search(r"getHostStatus calls: [0-9]{5}", traffic_out) is None, traffic_out
