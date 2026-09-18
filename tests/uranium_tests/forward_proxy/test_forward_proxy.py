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

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, ServiceFactory, VerifierServer


class ForwardProxyScenario:
    """Send an HTTP URL through an HTTPS connection to ATS as a forward proxy."""

    def __init__(self, policy: int | None, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._policy = policy
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> VerifierServer:
        """Provide the HTTP origin selected by forward proxying."""

        return services.verifier_server("origin", "forward_proxy.replay.yaml", https_ports=[])

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure TLS ingress and the requested scheme/protocol mismatch policy."""

        ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.http_port}/")
        ats.records.update(
            {
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
            })
        if self._policy is not None:
            ats.records.update({"proxy.config.ssl.client.scheme_proto_mismatch_policy": self._policy})
        return ats

    def verify(self, result: CommandResult) -> None:
        """Require rejection under strict policies and forwarding otherwise."""

        assert result.returncode == 0, result.output
        origin_output = self._origin.output
        if self._policy in (None, 2):
            assert "< HTTP/1.1 400 Invalid HTTP Request" in result.output
            assert "Received an HTTP/1 request with key 1" not in origin_output
        else:
            assert "< HTTP/1.1 200 OK" in result.output
            assert "Received an HTTP/1 request with key 1" in origin_output

    def run(self) -> None:
        """Start the origin and ATS, then issue the HTTPS-proxy request."""

        self._origin.start()
        self._ats.start()
        result = self._curl.run_for(
            self._ats,
            (
                f"--proxy-insecure --verbose --header 'uuid: 1' --proxy 'https://127.0.0.1:{self._ats.https_port}/' "
                f"http://example.com/"),
        )
        self.verify(result)


@pytest.mark.parametrize("policy", (None, 0, 1, 2), ids=("default", "permissive", "enforced", "strict"))
def test_forward_proxy(policy: int | None, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS applies its configured scheme/protocol mismatch policy as a forward proxy."""

    if curl.uses_uds:
        pytest.skip("the HTTPS proxy requires a TCP listener")
    ForwardProxyScenario(policy, ats_factory, services, curl).run()
