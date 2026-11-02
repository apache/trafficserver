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

from tools.uranium.services import ATSFactory, Curl


def test_explicit_listener_configuration(ats_factory: ATSFactory, curl: Curl) -> None:
    """Run two ATS instances with independently configured listeners.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param curl: Transport-aware curl command runner.
    """
    _first = ats_factory.create("ts1")
    _first.records.update({
        "proxy.config.http.server_ports": f"{_first.http_port} {_first.uds_path}",
    })
    _second = ats_factory.create("ts2")
    _second.records.update({"proxy.config.http.server_ports": str(_second.http_port)})
    _first.start()
    _second.start()
    first = curl.get(_first)
    second = curl.run(f"http://127.0.0.1:{_second.http_port}/")

    assert first.returncode == 0, first.output
    assert second.returncode == 0, second.output
