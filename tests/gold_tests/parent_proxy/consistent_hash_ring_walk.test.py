"""
Verify ParentConsistentHash::selectParent() does not waste work walking an
all-down consistent_hash pool.

A consistent-hash ring has 1024 replica nodes per parent (default
ATSConsistentHash replicas, DEFAULT_PARENT_WEIGHT=1.0), so num_parents*1024 ring
nodes total. When every parent is down, selectParent must walk the ring to
conclude so -- but it must read each parent's HostStatus (the global
host_status_rwlock) at most ONCE, not once per replica node. selectParent tracks
the distinct parents examined and stops as soon as all are rejected, so an
all-down selection over N parents costs exactly N getHostStatus calls -- not the
~2*N*1024 (two ring passes) the naive walk cost, which put ~8-13 ms of inline
ET_NET CPU per request and wedged the vipd health probe in inc-p1s2-260703.

Proven by the per-selection "getHostStatus calls: <N>" debug line
(ParentConsistentHash.cc): it must equal the parent count, not a five-figure ring
walk.

Pool size > MAX_PARENTS (64) on purpose: num_parents is NOT capped at
MAX_PARENTS by the config parser, so the per-selection "seen parent" tracking
must be sized to the actual pool (a fixed [MAX_PARENTS] array would overflow).
Running a >64-parent pool to completion (no crash, correct 502, bounded count)
guards that sizing.

no_dns_just_forward_to_parent=1 lets ATS skip origin resolution and go straight
to parent selection, so this test needs no DNS server and no origin -- every
parent is HostStatus-DOWN => PARENT_FAIL => 502 before any connect.
"""
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

import os

from ports import get_port

Test.Summary = '''
An all-down consistent_hash parent pool (marked down via HostStatus) drives
selectParent to read each distinct parent's HostStatus exactly once
(O(num_parents)), not once per ring replica node (~2 * num_parents * 1024).
'''
Test.ContinueOnFail = True

# > MAX_PARENTS (64): exercises the pool-sized "seen parent" tracking and would
# overflow a fixed [MAX_PARENTS] array. Distinct names matter -- consistent_hash
# keys the ring on hostname, so identical names would collapse to one ring node.
NUM_PARENTS = 100


class ParentDownRingWalkTest:
    """All parents HostStatus-DOWN => selectParent reads each parent once => 502."""

    parent_hostnames = [f'deadparent{i:03d}' for i in range(1, NUM_PARENTS + 1)]

    def __init__(self):
        self._setupTS()

    def _setupTS(self):
        self.ts = Test.MakeATSProcess('ts', enable_cache=False)

        # A dead (reserved-but-unbound) port per parent. They are never actually
        # connected -- every parent is HostStatus-DOWN so selectParent returns
        # PARENT_FAIL before any connect -- but parent.config needs a port.
        self._parent_ports = []
        for i in range(len(self.parent_hostnames)):
            name = f'dead_parent_port_{i}'
            get_port(self.ts, name)
            self._parent_ports.append(getattr(self.ts.Variables, name))

        self.ts.Disk.records_config.update(
            {
                # Enable only the parent_select debug ctl so the per-selection
                # "getHostStatus calls: <N>" line is emitted.
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'parent_select',
                # Skip origin DNS: forward straight to parent selection. No DNS
                # server / origin needed for this isolation.
                'proxy.config.http.no_dns_just_forward_to_parent': 1,
                # self_detect off: this test populates the HostStatus map via
                # `traffic_ctl host down` below, not via self_detect (the
                # deadparents never resolve to this box anyway).
                'proxy.config.http.parent_proxy.fail_threshold': 10,
                'proxy.config.http.parent_proxy.retry_time': 300,
                'proxy.config.http.parent_proxy.self_detect': 0,
                'proxy.config.url_remap.remap_required': 0,
            })

        # Consistent-hash pool of distinct parent hostnames, go_direct=false so an
        # all-down pool yields 502 (not a direct-to-origin fallback).
        self._parents = list(zip(self.parent_hostnames, self._parent_ports))
        parent_list = ', '.join(f'{host}:{port}|1' for host, port in self._parents)
        self.ts.Disk.parent_config.AddLine(
            f'dest_domain=. parent="{parent_list}" round_robin=consistent_hash '
            'go_direct=false parent_is_proxy=true')

        # The all-down selection is a single findParent call. selectParent reads
        # each distinct parent's HostStatus at most once and stops once all are
        # rejected, so the per-selection "getHostStatus calls" count equals the
        # parent count -- not the ~2*N*1024 five-figure ring walk it cost before.
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            r'getHostStatus calls: %d\b' % NUM_PARENTS,
            'selectParent read each distinct parent once (O(num_parents)), not the full ring.')
        # It must NOT take the HostStatus lock once per ring replica node (the old
        # bug reported a five-figure count).
        self.ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            r'getHostStatus calls: [0-9]{5}', 'selectParent must not read HostStatus once per ring replica node.')

    def run(self):
        traffic_ctl = os.path.join(self.ts.Variables.BINDIR, 'traffic_ctl')

        # 1. Bring the whole parent pool DOWN via HostStatus (one RPC call).
        #    Populates hosts_statuses (=> every getHostStatus takes the rwlock)
        #    and forces host_stat==DOWN (=> the full ring walk, no early exit).
        down = Test.AddTestRun('Mark the entire parent pool down via HostStatus')
        down.Processes.Default.StartBefore(self.ts)
        down.Processes.Default.Command = f'{traffic_ctl} host down ' + ' '.join(self.parent_hostnames)
        down.Processes.Default.Env = self.ts.Env
        down.Processes.Default.ReturnCode = 0

        # 2. One request through the all-down pool -> bounded walk -> 502. A crash
        #    here (e.g. seen-parent tracking overflowing on a >64 pool) fails the test.
        load = Test.AddTestRun('Request through the all-down pool -> bounded selection -> 502')
        load.MakeCurlCommand(
            f'-s -o /dev/null -w "%{{http_code}}" --proxy 127.0.0.1:{self.ts.Variables.port} http://example.com/ring-walk-probe',
            ts=self.ts)
        load.Processes.Default.Streams.stdout = Testers.ContainsExpression('502', 'All parents down => 502.')
        load.Processes.Default.ReturnCode = 0
        load.StillRunningAfter = self.ts


ParentDownRingWalkTest().run()
