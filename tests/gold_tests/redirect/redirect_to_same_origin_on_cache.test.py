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

import functools
from typing import Any, Callable, Dict, Optional

from ports import get_port

Test.Summary = 'Tests SNI port-based routing'

TestParams = Dict[str, Any]


class TestRedirectToSameOriginOnCache:
    """Configure a test for reproducing #9275."""

    replay_filepath_one: str = "redirect_to_same_origin_on_cache.replay.yaml"
    client_counter: int = 0
    server_counter: int = 0
    ts_counter: int = 0

    def __init__(self, name: str, /, autorun) -> None:
        """Initialize the test.

        :param name: The name of the test.
        """
        self.name = name
        self.autorun = autorun

    def _init_run(self) -> "TestRun":
        """Initialize processes for the test run."""

        tr = Test.AddTestRun(self.name)
        server_one = TestRedirectToSameOriginOnCache.configure_server(tr, "oof.com")
        self._configure_traffic_server(tr, server_one)
        dns = TestRedirectToSameOriginOnCache.configure_dns(tr, server_one, self._dns_port)

        tr.Processes.Default.StartBefore(server_one)
        tr.Processes.Default.StartBefore(dns)
        tr.Processes.Default.StartBefore(self._ts)

        return {
            "tr": tr,
            "ts": self._ts,
            "server_one": server_one,
            "port_one": self._port_one,
        }

    @classmethod
    def runner(cls, name: str, autorun: bool = True) -> Optional[Callable]:
        """Create a runner for a test case.

        :param autorun: Run the test case once it's set up. Default is True.
        """
        test = cls(name, autorun=autorun)._prepare_test_case
        return test

    def _prepare_test_case(self, func: Callable) -> Callable:
        """Set up a test case and possibly run it.

        :param func: The test case to set up.
        """
        functools.wraps(func)
        test_params = self._init_run()

        def wrapper(*args, **kwargs) -> Any:
            return func(test_params, *args, **kwargs)

        if self.autorun:
            wrapper()
        return wrapper

    @staticmethod
    def configure_server(tr: "TestRun", domain: str):
        server = tr.AddVerifierServerProcess(
            f"server{TestRedirectToSameOriginOnCache.server_counter}.{domain}",
            TestRedirectToSameOriginOnCache.replay_filepath_one,
            other_args="--format \"{url}\"")
        TestRedirectToSameOriginOnCache.server_counter += 1

        return server

    @staticmethod
    def configure_dns(tr: "TestRun", server_one: "Process", dns_port: int):
        dns = tr.MakeDNServer("dns", port=dns_port, default="127.0.0.1")
        return dns

    def _configure_traffic_server(self, tr: "TestRun", server_one: "Process"):
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        ts = tr.MakeATSProcess(
            f"ts-{TestRedirectToSameOriginOnCache.ts_counter}", select_ports=False, enable_tls=True, enable_cache=True)
        TestRedirectToSameOriginOnCache.ts_counter += 1

        self._port_one = get_port(ts, "PortOne")
        self._dns_port = get_port(ts, "DNSPort")
        ts.Disk.records_config.update(
            {
                'proxy.config.http.server_ports': f"{self._port_one}",
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': "cache|dns|http|redirect|remap",
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns_port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.http.redirect.actions': "self:follow",
                'proxy.config.http.number_of_redirections': 1,
            })

        ts.Disk.remap_config.AddLine(f"map oof.com http://oof.backend.com:{server_one.Variables.http_port}")

        self._ts = ts


# Tests start.


@TestRedirectToSameOriginOnCache.runner("Redirect to same origin with cache")
def test1(params: TestParams) -> None:
    client = params["tr"].AddVerifierClientProcess(
        f"client0",
        TestRedirectToSameOriginOnCache.replay_filepath_one,
        http_ports=[params["port_one"]],
        other_args="--format \"{url}\" --keys \"/a/path/resource\"")

    params["tr"].Processes.Default.ReturnCode = 0
    params["tr"].Processes.Default.Streams.stdout.Content += Testers.IncludesExpression("200 OK", "We should get the resource.")
