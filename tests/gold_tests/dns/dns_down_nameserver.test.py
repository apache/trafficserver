'''
Verify ATS handles down name servers correctly.
'''
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

from ports import get_port


# This value tracks DNS_PRIMARY_RETRY_PERIOD in P_DNSProcessor.h.
DNS_PRIMARY_RETRY_PERIOD = 5

Test.Summary = '''
Verify ATS handles down name servers correctly.
'''


class DownDNSNameserverTest:
    """Encapsulate logic for testing down name server functionality.

    Note that this test verifies that ATS behaves correctly with respect to
    down *name* servers. This is different than testing down origin servers,
    which is an entirely different feature and implementation.
    """

    _replay_file = "replay/multiple_host_requests.replay.yaml"

    def __init__(self):
        """Initialize the Test processes for the test runs."""
        self._dns_port = None
        self._configure_origin_server()
        self._configure_traffic_server()
        self._servers_are_running = False

        self._client_counter = 0
        self._dns_counter = 0

    def _configure_origin_server(self):
        """Configure the origin server and the DNS port."""
        self._server = Test.MakeVerifierServerProcess("server", self._replay_file)

        # Associate the DNS port with the long-running server. This way
        # ATS can be configured to use it across the multiple test runs
        # and DNS servers.
        self._dns_port = get_port(self._server, "DNSPort")

    def _configure_traffic_server(self):
        """Configure Traffic Server."""
        if not self._dns_port:
            raise RuntimeError("The DNS port was not configured.")

        # Since we are testing DNS name resolution and whether these
        # transactions reach the origin, disable the caching to ensure none of
        # the responses come out of the cache without going to the origin.
        self._ts = Test.MakeATSProcess("ts", enable_cache=False)

        self._ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'hostdb|dns',
            'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns_port}',
            'proxy.config.dns.resolv_conf': 'NULL'
        })

        # Cause a name resolution for each, unique path.
        self._ts.Disk.remap_config.AddLines([
            f'map /first/host http://first.host.com:{self._server.Variables.http_port}/',
            f'map /second/host http://second.host.com:{self._server.Variables.http_port}/',
            f'map /third/host http://third.host.com:{self._server.Variables.http_port}/',
        ])

    def _run_transaction(self, start_dns: bool, keyname: str):
        """Run a transaction with the name server reachable.

        :param start_dns: Whether the TestRun should configure a name server to
        be running for ATS to talk to.

        :param keyname: The identifier for the transaction to run.
        """
        tr = Test.AddTestRun()

        tr.AddVerifierClientProcess(
            f'client{self._client_counter}',
            self._replay_file,
            http_ports=[self._ts.Variables.port],
            other_args=f'--keys {keyname}')
        self._client_counter += 1

        if start_dns:
            dns = tr.MakeDNServer(
                f'dns{self._dns_counter}',
                default='127.0.0.1',
                port=self._dns_port)
            self._dns_counter += 1
            tr.Processes.Default.StartBefore(dns)

        if not self._servers_are_running:
            tr.Processes.Default.StartBefore(self._server)
            tr.Processes.Default.StartBefore(self._ts)
            self._servers_are_running = True

        # Verify that the client tried to send the transaction.
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f'uuid: {keyname}',
            f'The client should have sent a transaction with uuid {keyname}')

        # The client will report an error if ATS could not complete the
        # transaction due to DNS resolution issues.
        if not start_dns:
            tr.Processes.Default.ReturnCode = 1

    def _delay_for_dns_retry_period(self, start_dns: bool):
        """Wait for the failed DNS retry period."""
        tr = Test.AddTestRun()

        if start_dns:
            dns = tr.MakeDNServer(
                f'dns{self._dns_counter}',
                default='127.0.0.1',
                port=self._dns_port)
            self._dns_counter += 1
            tr.Processes.Default.StartBefore(dns)

        # Exceed the retry period.
        tr.Processes.Default.Command = f'sleep {DNS_PRIMARY_RETRY_PERIOD + 1}'

    def _test_dns_reachable(self):
        """This is the base case: DNS is reachable.

        This verifies that ATS can use the name server to resolve a name when
        the name server is reachable.
        """
        self._run_transaction(start_dns=True, keyname='first_host')

    def _test_dns_reachable_within_the_retry_period(self):
        """Test nameserver connectivity resolution within the retry period.

        Have the name server be inaccessible, but bring it back online within
        the retry period.
        """
        # Trigger a name resolution with the name server down.
        self._run_transaction(start_dns=False, keyname='second_host')

        # Wait long enough that ATS to retry the name server, with the server
        # up.
        self._delay_for_dns_retry_period(start_dns=True)

        # ATS should now use the working name server again to resolve the host
        # name.
        self._run_transaction(start_dns=True, keyname='second_host')

    def _test_dns_reachable_outside_the_retry_period(self):
        """Test nameserver connectivity resolution outside the retry period.

        Have the name server be inaccessible, but bring it back online outside
        the retry period. This verifies that we handle it coming back online
        after the first attempt to re-connect to it.
        """
        # Trigger a name resolution with the name server down.
        self._run_transaction(start_dns=False, keyname='third_host')

        # Wait long enough that ATS to retry connectivity to the name server,
        # with the server down. This will naturally fail.
        self._delay_for_dns_retry_period(start_dns=False)

        # Wait long enough that ATS to retry connectivity a second time to name
        # server, this time with the server up.
        self._delay_for_dns_retry_period(start_dns=True)

        # ATS should now try the name server again, detect that it is up, and
        # complete the transaction.
        self._run_transaction(start_dns=True, keyname='third_host')

    def run(self):
        """Exercise ATS down name server functionality."""
        self._test_dns_reachable()
        self._test_dns_reachable_within_the_retry_period()
        self._test_dns_reachable_outside_the_retry_period()


DownDNSNameserverTest().run()
