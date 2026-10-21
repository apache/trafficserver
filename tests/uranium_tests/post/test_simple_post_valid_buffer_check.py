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

from tools.uranium.services import ATS, ATSFactory, Curl, HttpBinServer, ServiceFactory, wait_for_file_lines


def configure_origin(services: ServiceFactory) -> HttpBinServer:
    """Create the HTTPBin POST endpoint.

    :param services: Factory owning support services and their cleanup.
    """

    return services.httpbin("origin")


def configure_ats(ats_factory: ATSFactory, *, _origin: HttpBinServer) -> ATS:
    """Enable request buffering and HTTP debug diagnostics.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.http.request_buffer_enabled": 1,
            "proxy.config.http.number_of_redirections": 1,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    return ats


def test_simple_post_valid_buffer_check(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A buffered 100-continue POST does not leave ATS without a write buffer.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    result = curl.run_for(
        _ats,
        f"--verbose --header 'Expect: 100-continue' --data abc 'http://127.0.0.1:{_ats.http_port}/post'",
    )
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200 OK" in result.stderr, result.output
    output = wait_for_file_lines(_ats.traffic_out, r"HTTP/1\.1 100 Continue", 1)
    assert "HTTP/1.1 200 OK" in output
