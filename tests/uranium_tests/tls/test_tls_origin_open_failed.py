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

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory


def configure_ats(ats_factory: ATSFactory, *, _origin_port: int) -> ATS:
    """Bind outbound sockets to a non-local documentation address.

    :param _origin_port: Test-local origin port configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.remap_config.add_line(f"map http://dead.test/ https://127.0.0.1:{_origin_port}/")
    ats.records.update(
        {
            "proxy.config.outgoing_ip_to_bind": "192.0.2.1",
            "proxy.config.http.connect_attempts_max_retries": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|ssl",
        })
    return ats


def test_tls_origin_open_failed(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A failed outbound TLS open yields a 5xx without crashing ATS.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin_port = services.allocate_port()
    _ats = configure_ats(ats_factory, _origin_port=_origin_port)

    _ats.start()
    result = curl.get(
        _ats,
        headers={"Host": "dead.test"},
        options=f"--silent --output /dev/null --write-out '%{{http_code}}'",
    )
    assert result.returncode == 0, result.output
    assert re.fullmatch(r"50[02]", result.stdout)
    assert _ats.is_running
    traffic_out = _ats.traffic_out.read_text(errors="replace")
    assert re.search(r"received signal|failed assertion", traffic_out) is None
