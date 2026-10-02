'''
Verify Split DNS fails over to a healthy configured nameserver.
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

Test.Summary = 'Verify Split DNS fails over to a healthy nameserver.'


class SplitDNSFailoverTest:

    def __init__(self):
        self.primary_dns = Test.MakeDNServer('primary_dns')
        self.secondary_dns = Test.MakeDNServer('secondary_dns')
        self.secondary_dns.addRecords(records={'foo.ts.a.o.': ['127.0.0.1']})

        self.origin = Test.MakeOriginServer('origin')
        self.origin.addResponse(
            'sessionlog.json', {'headers': 'GET / HTTP/1.1\r\nHost: foo.ts.a.o\r\n\r\n'},
            {'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n'})

        self.ts = Test.MakeATSProcess('ts', enable_cache=False)
        self.ts.Disk.records_config.update(
            {
                'proxy.config.dns.splitDNS.enabled': 1,
                'proxy.config.dns.round_robin_nameservers': 1,
                'proxy.config.dns.resolv_conf': 'NULL',
            })
        self.ts.Disk.splitdns_config.AddLine(
            f'dest_domain=foo.ts.a.o named="127.0.0.1:{self.primary_dns.Variables.Port} '
            f'127.0.0.1:{self.secondary_dns.Variables.Port}"')
        self.ts.Disk.remap_config.AddLine(f'map /foo/ http://foo.ts.a.o:{self.origin.Variables.Port}/')

    def run(self):
        tr = Test.AddTestRun()
        tr.MakeCurlCommand(f'-sS -i http://localhost:{self.ts.Variables.port}/foo/', ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            'HTTP/1.1 200 OK', 'Split DNS should fail over to the second configured nameserver')
        tr.Processes.Default.StartBefore(self.secondary_dns)
        tr.Processes.Default.StartBefore(self.origin)
        tr.Processes.Default.StartBefore(self.ts)


SplitDNSFailoverTest().run()
