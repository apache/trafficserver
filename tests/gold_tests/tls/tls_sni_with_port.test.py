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
from typing import Any, Callable, Optional

from ports import get_port

Test.Summary = 'Tests SNI port-based routing'


class TestSNIWithPort:
    """Configure a test for SNI port-based routing ."""

    replay_filepath: str = "tls_sni_with_port.replay.yaml"
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
        server_one = TestSNIWithPort.configure_server(tr, "yay.com")
        server_two = TestSNIWithPort.configure_server(tr, "oof.com")
        server_three = TestSNIWithPort.configure_server(tr, "wow.com")
        self._configure_traffic_server(tr, server_one, server_two, server_three)

        tr.Processes.Default.StartBefore(server_one)
        tr.Processes.Default.StartBefore(server_two)
        tr.Processes.Default.StartBefore(self._ts)

        return tr, self._ts, server_one, server_two, server_three, self._port_one, self._port_two, self._unspecified_port

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
        tr, *test_run_args = self._init_run()

        def wrapper(*args, **kwargs) -> Any:
            return func(tr, *test_run_args, *args, **kwargs)

        if self.autorun:
            wrapper()
        return wrapper

    @staticmethod
    def configure_server(tr: "TestRun", domain: str):
        server = tr.AddVerifierServerProcess(
            f"server{TestSNIWithPort.server_counter}.{domain}",
            TestSNIWithPort.replay_filepath
        )
        TestSNIWithPort.server_counter += 1

        return server

    def _configure_traffic_server(self, tr: "TestRun", server_one: "Process", server_two: "Process", server_three: "Process"):
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        ts = tr.MakeATSProcess(f"ts-{TestSNIWithPort.ts_counter}", select_ports=False, enable_tls=True)
        TestSNIWithPort.ts_counter += 1

        ts.addDefaultSSLFiles()
        self._port_one = get_port(ts, "PortOne")
        self._port_two = get_port(ts, "PortTwo")
        self._unspecified_port = get_port(ts, "UnspecifiedPort")
        ts.Disk.records_config.update({
            'proxy.config.ssl.server.cert.path': f"{ts.Variables.SSLDir}",
            'proxy.config.ssl.server.private_key.path': f"{ts.Variables.SSLDir}",
            'proxy.config.http.server_ports': f"{self._port_one}:ssl {self._port_two}:ssl {self._unspecified_port}:ssl",
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'dns|http|ssl|sni',
        })

        ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{server_three.Variables.http_port}")

        ts.Disk.sni_yaml.AddLines([
            "sni:",
            f"- fqdn: yay.example.com:{self._port_one}-{self._port_one}",
            f"  tunnel_route: localhost:{server_one.Variables.https_port}",
            f"- fqdn: yay.example.com:{self._port_two}-{self._port_two}",
            f"  tunnel_route: localhost:{server_two.Variables.https_port}"
        ])

        ts.Disk.ssl_multicert_config.AddLine(
            f"dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key"
        )

        self._ts = ts


# Tests start.

@TestSNIWithPort.runner("Test that a request to a port not in the SNI does not get through.")
def test0(
        tr: "TestRun",
        ts: "Process",
        server_one: "Process",
        server_two: "Process",
        server_three: "Process",
        port_one: int,
        port_two: int,
        unspecified_port: int):
    client = tr.AddVerifierClientProcess(
        f"client0",
        TestSNIWithPort.replay_filepath,
        https_ports=[unspecified_port],
        keys="conn_remapped"
    )

    tr.Processes.Default.ReturnCode = 0
    ts.Disk.traffic_out.Content += Testers.IncludesExpression(
        "not available in the map", "the request should not match an SNI"
    )
    server_one.Streams.All.Content += Testers.ExcludesExpression(
        "Received an HTTP/1 Content-Length body of 16 bytes for key conn_remapped", "the request should not go to server one"
    )
    server_two.Streams.All.Content += Testers.ExcludesExpression(
        "Received an HTTP/1 Content-Length body of 16 bytes for key conn_remapped", "the request should not go to server two"
    )
    server_three.Streams.All.Content += Testers.IncludesExpression(
        "Received an HTTP/1 Content-Length body of 16 bytes for key conn_remapped", "request was remaped to server three"
    )


@TestSNIWithPort.runner("Test that a request to a port one goes to server one.")
def test1(
        tr: "TestRun",
        ts: "Process",
        server_one: "Process",
        server_two: "Process",
        server_three: "Process",
        port_one: int,
        port_two: int,
        unspecified_port: int):
    client = tr.AddVerifierClientProcess(
        f"client1",
        TestSNIWithPort.replay_filepath,
        https_ports=[port_one],
        keys="conn_accepted"
    )

    tr.Processes.Default.ReturnCode = 0
    server_one.Streams.All.Content += Testers.IncludesExpression(
        "Received an HTTP/1 Content-Length body of 16 bytes for key conn_accepted", "the request should go to server one"
    )
    server_two.Streams.All.Content += Testers.ExcludesExpression(
        "Received an HTTP/1 Content-Length body of 16 bytes for key conn_accepted", "the request should not go to server two"
    )


@TestSNIWithPort.runner("Test that a request to port two goes to server two.")
def test2(
        tr: "TestRun",
        ts: "Process",
        server_one: "Process",
        server_two: "Process",
        server_three: "Process",
        port_one: int,
        port_two: int,
        unspecified_port: int):
    client = tr.AddVerifierClientProcess(
        f"client2",
        TestSNIWithPort.replay_filepath,
        https_ports=[port_two],
        keys="conn_accepted"
    )

    tr.Processes.Default.ReturnCode = 0
    server_two.Streams.All.Content += Testers.IncludesExpression(
        "Received an HTTP/1 Content-Length body of 16 bytes for key conn_accepted", "the request should go to server two"
    )
    server_one.Streams.All.Content += Testers.ExcludesExpression(
        "Received an HTTP/1 Content-Length body of 16 bytes for key conn_accepted", "the request should not go to server one"
    )
