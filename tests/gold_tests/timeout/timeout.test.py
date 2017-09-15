'''
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

import os
import subprocess

Test.Summary = 'Testing ATS timeout'

netns_name = Test.Name
TIMEOUT = 3

# Test.SkipIf(Condition.true("Skipping this test since running it requires superuser privilege, which introduces other problems"))

if os.geteuid() != 0:
    Test.SkipIf(Condition.true("Must be run with superuser privileges"))

# Fix: not stable
Test.SkipUnless(
    Condition.HasProgram("/usr/sbin/ip", "ip (from iproute2) must be installed.")
)
Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work")
)

# each of these should return 0, which is default pass value of RunCommand anyways
Setup.RunCommand("ip netns add {0}".format(netns_name))
Setup.RunCommand("ip netns exec {0} ip link set dev lo up".format(netns_name))
Setup.RunCommand("ip netns exec {0} iptables -A OUTPUT -p tcp --tcp-flags RST RST -j DROP".format(netns_name))

# run traffic_server under the namespace with the default ports
ts = Test.MakeATSProcess(name="ts", command="ip netns exec timeout traffic_server", select_ports=False)

ts.Disk.records_config.update({
    'proxy.config.http.connect_attempts_timeout': TIMEOUT,
    'proxy.config.http.connect_attempts_max_retries': 1,
    'proxy.config.http.connect_attempts_rr_retries': 1,
    'proxy.config.http.cache.http': 0,
    'proxy.config.diags.debug.enabled': 1
})

# assumption: port 54321 isn't occupied in the namespace (no reason why it would be occupied)
ts.Disk.remap_config.AddLine(
    'map http://127.0.0.1:{0} http://127.0.0.1:54321'.format(ts.Variables.port)
)

tr = Test.AddTestRun()
# can confirm with wireshark
tr.Processes.Default.Command = 'ip netns exec {0} curl -i http://127.0.0.1:{1}'.format(netns_name, ts.Variables.port)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stdout = "gold/timeout.gold"
tr.Processes.Default.ReturnCode = 0


def cleanup_netns():
    subprocess.call(["ip", "netns", "del", "{0}".format(netns_name)])


Setup.Lambda(func_cleanup=cleanup_netns, description="Cleaning up namespace")
