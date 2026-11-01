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

from tools.uranium.services import ATS, Curl


def configure_traffic_server(*, _ats: ATS) -> None:
    """Custom listener configure traffic server.

    :param _ats: Test-local ats configured by the test.
    """
    _ats.records.update({"proxy.config.http.server_ports": f"{_ats.http_port} {_ats.uds_path}"})


def start_traffic_server(*, _ats: ATS) -> None:
    """Custom listener start traffic server.

    :param _ats: Test-local ats configured by the test.
    """
    _ats.start()


def verify_custom_listener_accepts_requests(*, _ats: ATS, _curl: Curl) -> None:
    """Custom listener verify custom listener accepts requests.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """
    result = _curl.get(_ats)

    assert result.returncode == 0, result.output


def test_traffic_server_starts_with_custom_listener(ats: ATS, curl: Curl) -> None:
    """Verify that records.yaml can replace the default listener configuration.

    :param ats: Traffic Server instance configured or queried by this step.
    :param curl: Transport-aware curl command runner.
    """
    configure_traffic_server(_ats=ats)
    start_traffic_server(_ats=ats)
    verify_custom_listener_accepts_requests(_ats=ats, _curl=curl)
