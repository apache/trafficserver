'''
Test DNS TTL behavior.
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

import ports

Test.Summary = 'Test DNS TTL behavior'


class TtlDnsTest:
    single_transaction_replay = "replay/single_transaction.replay.yaml"
    server_error_replay = "replay/server_error.replay.yaml"
    process_counter = 1

    # The TTL to set for every resolved hostname.
    dnsTTL = 1

    # The DNS query timeout.
    queryTimeout = 1

    def __init__(self, configure_serve_stale=False, exceed_serve_stale=False):
        """
        Args:
            configure_serve_stale: (bool) Whether the ATS process should be configured to
            serve stale DNS entries.

            exceed_serve_stale: (bool) Configure the serve_stale timeout to be low
            enough that the timed out DNS response will not be used.
        """
        self.configure_serve_stale = configure_serve_stale
        self.exceed_serve_stale = exceed_serve_stale

        self.server_process_counter = TtlDnsTest.get_unique_process_counter()
        TtlDnsTest.process_counter += 1

        self.setupOriginServer()
        self.setupTS()

    @classmethod
    def get_unique_process_counter(cls):
        this_counter = cls.process_counter
        cls.process_counter += 1
        return this_counter

    def addDNSServerToTestRun(self, test_run):
        dns = test_run.MakeDNServer("dns", port=self.dns_port)
        dns.addRecords(records={'resolve.this.com': ['127.0.0.1']})
        return dns

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess(
            f"server-{self.server_process_counter}", TtlDnsTest.single_transaction_replay)

    def setupTS(self):
        self.ts = Test.MakeATSProcess(
            f"ts-{self.server_process_counter}", select_ports=True, enable_cache=False)
        self.dns_port = ports.get_port(self.ts, 'dns_port')
        self.ts.Disk.records_config.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "dns",

            'proxy.config.dns.nameservers': f'127.0.0.1:{self.dns_port}',
            'proxy.config.dns.resolv_conf': 'NULL',

            # Configure ATS to treat each resolved name to have a 1 second
            # time to live.
            "proxy.config.hostdb.ttl_mode": 1,
            "proxy.config.hostdb.timeout": self.dnsTTL,

            # MicroDNS will be down for the second transaction. Have ATS give
            # up trying to talk to it after one second.
            "proxy.config.hostdb.lookup_timeout": self.queryTimeout,
        })
        if self.configure_serve_stale:
            if self.exceed_serve_stale:
                stale_timeout = 1
            else:
                stale_timeout = 300

            self.ts.Disk.records_config.update({
                "proxy.config.hostdb.serve_stale_for": stale_timeout
            })
        self.ts.Disk.remap_config.AddLine(
            f"map / http://resolve.this.com:{self.server.Variables.http_port}/")

    def testRunWithDNS(self):
        tr = Test.AddTestRun()

        # Run the DNS server with this test run so it will not be running in
        # the next one.
        dns = self.addDNSServerToTestRun(tr)
        process_number = TtlDnsTest.get_unique_process_counter()
        tr.AddVerifierClientProcess(
            f"client-{process_number}",
            TtlDnsTest.single_transaction_replay,
            http_ports=[self.ts.Variables.port])

        tr.Processes.Default.StartBefore(dns)
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)

        tr.StillRunningAfter = dns
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def testRunWithoutDNS(self):
        tr = Test.AddTestRun()

        # Delay running the second transaction for long enough to guarantee
        # that both the TTL and the DNS query timeout (lookup_timeout) are
        # exceeded.
        tr.DelayStart = 3

        # Will the stale resolved DNS response be used?
        if self.configure_serve_stale and not self.exceed_serve_stale:
            # Yes: expect a proxied transaction with a 200 OK response.
            replay_file = TtlDnsTest.single_transaction_replay
        else:
            # No: expect a 5xx response because the server name could not be
            # resolved.
            replay_file = TtlDnsTest.server_error_replay
        process_number = TtlDnsTest.get_unique_process_counter()
        tr.AddVerifierClientProcess(
            f"client-{process_number}",
            replay_file, http_ports=[self.ts.Variables.port])

        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.testRunWithDNS()
        self.testRunWithoutDNS()


TtlDnsTest().run()
TtlDnsTest(configure_serve_stale=True, exceed_serve_stale=False).run()
TtlDnsTest(configure_serve_stale=True, exceed_serve_stale=True).run()
