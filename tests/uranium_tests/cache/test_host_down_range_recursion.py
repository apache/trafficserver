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

from tools.uranium.services import ATS, ServiceFactory
from uranium_tests.lib.jsonrpc import Request


def test_host_down_range_recursion(ats: ATS, services: ServiceFactory) -> None:
    """An unsatisfied Range request to a DOWN host does not recurse forever.

    :param ats: Traffic Server instance configured or queried by this step.
    :param services: Factory owning support services and their cleanup.
    """

    _PRIME_REPLAY = "replay/host_down_range_recursion_prime.replay.yaml"
    _RANGE_REPLAY = "replay/host_down_range_recursion_range.replay.yaml"
    origin = services.verifier_server("server", _PRIME_REPLAY)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|host_statuses",
            "proxy.config.http.cache.range.write": 1,
            "proxy.config.http.insert_response_via_str": 3,
        })
    ats.remap_config.add_line(f"map http://backend.example.com/ http://127.0.0.1:{origin.http_port}/")
    origin.start()
    ats.start()
    result = services.verifier_client("prime-client", _PRIME_REPLAY, http_ports=[ats.http_port]).run()

    assert result.returncode == 0, result.output
    result = ats.rpc(Request.admin_host_set_status(
        operation="down",
        host=["127.0.0.1"],
        reason="manual",
        time="0",
    ))

    assert result.returncode == 0, result.output
    result = services.verifier_client("range-client", _RANGE_REPLAY, http_ports=[ats.http_port]).run(timeout=10)

    assert result.returncode == 0, result.output
    assert ats.is_running
    assert origin.is_running
