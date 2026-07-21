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
import socket

Test.Summary = '''
Verify TSHttpTxnClientPacketMarkSet and TSHttpTxnServerPacketMarkSet set the
firewall mark on the client- and server-side connections respectively. Each is
driven by a test plugin that applies the mark and reads it back off the relevant
socket with getsockopt(SO_MARK), echoing the observed value into a response
header this test asserts on.
'''


def _can_set_so_mark() -> bool:
    """Probe whether SO_MARK can actually be set on this host.

    Setting SO_MARK is Linux-only and requires CAP_NET_ADMIN or CAP_NET_RAW.
    On any host that lacks the capability (or the platform), setsockopt raises,
    and the applied value would be unobservable -- so the test is skipped
    rather than failed.
    """
    if not hasattr(socket, "SO_MARK"):
        return False
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_MARK, 0x1)
        return True
    except (OSError, PermissionError):
        return False


Test.SkipUnless(
    Condition.IsPlatform("linux"),
    # pass_value defaults to True: run only when the probe reports SO_MARK is settable.
    Condition(_can_set_so_mark, "Setting SO_MARK requires Linux with CAP_NET_ADMIN or CAP_NET_RAW"),
)

# SOCK_OPT_PACKET_MARK (0x10) | SOCK_OPT_NO_DELAY (0x1). The mark is only pushed
# to the socket when the PACKET_MARK bit is set in the sock option flag.
SOCK_OPT_FLAG_PACKET_MARK = 0x11


class PacketMarkTest:
    """Drive a TSHttpTxn*PacketMarkSet API through a test plugin and assert on the
    firewall mark read back off the relevant socket.

    This base holds the shared skeleton -- per-run process setup, the common
    records, and the curl-and-assert case runners. Each subclass supplies its
    plugin and echo header and extends _configure() (via super()) with the
    side-specific mark and flag records.

    Every test run gets its own freshly-started origin server and ATS instance,
    each serving exactly one request. Runs are therefore fully isolated: process
    teardown or a crash in one run cannot disturb any other run.
    """

    # Value the plugin sets; the mark is expected to become exactly this.
    SET_MARK = 0x0000000A
    # Seeded starting mark, distinct from SET_MARK so a no-op would be visible.
    SEED_MARK = 0x0000FF00

    # Bumped per created process so each run gets uniquely-numbered processes.
    _counter = 0

    def __init__(self, seed_mark: int = None):
        # Instance-level override of the seeded starting mark; falls back to the
        # class default. Lets a masked case start from a distinct value (e.g. 0)
        # without disturbing the other runs.
        if seed_mark is not None:
            self.SEED_MARK = seed_mark

    def _make_instance(self):
        # Build a fresh origin server and ATS process for a single test run. Each
        # run owns its processes and serves exactly one request, so runs are fully
        # isolated from one another.
        num = PacketMarkTest._counter
        PacketMarkTest._counter += 1
        server = Test.MakeOriginServer(f"server{num}")
        request_header = {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        server.addResponse("sessionlog.json", request_header, response_header)
        ts = Test.MakeATSProcess(f"ts{num}", enable_cache=False)
        self._configure(ts, server)
        return server, ts

    def _configure(self, ts, server):
        # Records and remap shared by both sides. Subclasses override to add the
        # side-specific mark/flag records and load their plugin, calling super()
        # for these.
        ts.Disk.records_config.update(
            {
                'proxy.config.url_remap.remap_required': 0,
                # Keep ATS running as the invoking user inside sudo (no privilege drop).
                'proxy.config.admin.user_id': '#-1',
            })
        ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{server.Variables.Port}")

    def _add_case(self, echo_header: str, description: str, set_header: str):
        # The mark is set to the supplied value, regardless of the seeded starting
        # mark. The set is driven by whichever request header the plugin keys on;
        # the observed mark is echoed into echo_header. This case runs against its
        # own freshly-started origin server and ATS instance.
        server, ts = self._make_instance()
        tr = Test.AddTestRun(description)
        tr.Processes.Default.StartBefore(server)
        tr.Processes.Default.StartBefore(ts)
        tr.MakeCurlCommand(
            f'--verbose --ipv4 --header "{set_header}: 0x{self.SET_MARK:08x}" http://localhost:{ts.Variables.port}/', ts=ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f"{echo_header}: 0x{self.SET_MARK:08x}", f"Observed packet mark should be 0x{self.SET_MARK:08x}")
        tr.StillRunningAfter = server
        tr.StillRunningAfter = ts

    def _add_masked_case(self, echo_header: str, description: str, mark: int, mask: int, expected: int):
        # Drives the three-argument (masked) overload: only the bits selected by
        # `mask` are taken from `mark`; the rest retain the seeded starting mark.
        # `expected` is the full value the mark should read back as. The set is
        # keyed on X-Set-Mark + X-Set-Mask; the observed mark is echoed into
        # echo_header. This case runs against its own freshly-started instance.
        server, ts = self._make_instance()
        tr = Test.AddTestRun(description)
        tr.Processes.Default.StartBefore(server)
        tr.Processes.Default.StartBefore(ts)
        tr.MakeCurlCommand(
            f'--verbose --ipv4 --header "X-Set-Mark: 0x{mark:08x}" --header "X-Set-Mask: 0x{mask:08x}" '
            f'http://localhost:{ts.Variables.port}/',
            ts=ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f"{echo_header}: 0x{expected:08x}", f"Observed packet mark should be 0x{expected:08x}")
        tr.StillRunningAfter = server
        tr.StillRunningAfter = ts


class ClientPacketMarkTest(PacketMarkTest):
    """Exercise TSHttpTxnClientPacketMarkSet. The client mark is seeded on the
    inbound socket.
    """

    ECHO_HEADER = "X-Client-Packet-Mark"

    def _configure(self, ts, server):
        super()._configure(ts, server)
        ts.Disk.records_config.update(
            {
                'proxy.config.net.sock_packet_mark_in': self.SEED_MARK,
                'proxy.config.net.sock_option_flag_in': SOCK_OPT_FLAG_PACKET_MARK,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|client_packet_mark|plugin',
            })
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, f'client_packet_mark.so'), ts)

    def run(self):
        self._add_case(self.ECHO_HEADER, "client_packet_mark sets the client-side mark on the live connection", "X-Set-Mark")
        # Masked overload: only the bits selected by the mask change; the rest keep
        # the seeded starting mark (SEED_MARK = 0x0000FF00).
        self._add_masked_case(
            self.ECHO_HEADER, "client_packet_mark masked set updates only selected bits", 0x0000000A, 0x0000000F, 0x0000FF0A)
        self._add_masked_case(
            self.ECHO_HEADER, "client_packet_mark masked set with all-bits mask replaces the whole mark", self.SET_MARK, 0xFFFFFFFF,
            self.SET_MARK)
        self._add_masked_case(
            self.ECHO_HEADER, "client_packet_mark masked set with no-bits mask leaves the mark unchanged", self.SET_MARK,
            0x00000000, self.SEED_MARK)


class ClientPacketMarkZeroSeedTest(PacketMarkTest):
    """Exercise the masked overload from a zero starting mark, so the selected bits
    are the only bits set afterward.
    """

    ECHO_HEADER = "X-Client-Packet-Mark"

    def __init__(self):
        super().__init__(seed_mark=0x00000000)

    def _configure(self, ts, server):
        super()._configure(ts, server)
        ts.Disk.records_config.update(
            {
                'proxy.config.net.sock_packet_mark_in': self.SEED_MARK,
                'proxy.config.net.sock_option_flag_in': SOCK_OPT_FLAG_PACKET_MARK,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|client_packet_mark',
            })
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, f'client_packet_mark.so'), ts)

    def run(self):
        self._add_masked_case(
            self.ECHO_HEADER, "client_packet_mark masked set from a zero starting mark sets only the selected bits", 0x0000000A,
            0x0000000F, 0x0000000A)


class ServerPacketMarkTest(PacketMarkTest):
    """Exercise TSHttpTxnServerPacketMarkSet. The server mark is seeded on the
    outbound socket.

    The server API additionally records the mark for a *future* origin
    connection (TSHttpTxnConfigIntSet on TS_CONFIG_NET_SOCK_PACKET_MARK_OUT),
    which the client API has no equivalent of. The server plugin exposes this by
    honoring X-Set-Mark-Preconnect at READ_REQUEST_HDR, before any origin
    connection exists -- so the mark can only reach the socket via that seed.
    """

    ECHO_HEADER = "X-Server-Packet-Mark"

    def _configure(self, ts, server):
        super()._configure(ts, server)
        ts.Disk.records_config.update(
            {
                'proxy.config.net.sock_packet_mark_out': self.SEED_MARK,
                'proxy.config.net.sock_option_flag_out': SOCK_OPT_FLAG_PACKET_MARK,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|server_packet_mark',
            })
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, f'server_packet_mark.so'), ts)

    def run(self):
        self._add_case(self.ECHO_HEADER, "server_packet_mark sets the server-side mark on the live connection", "X-Set-Mark")
        self._add_case(
            self.ECHO_HEADER, "server_packet_mark seeds the mark for a future origin connection", "X-Set-Mark-Preconnect")
        # Masked overload: only the bits selected by the mask change; the rest keep
        # the seeded starting mark (SEED_MARK = 0x0000FF00).
        self._add_masked_case(
            self.ECHO_HEADER, "server_packet_mark masked set updates only selected bits", 0x0000000A, 0x0000000F, 0x0000FF0A)
        self._add_masked_case(
            self.ECHO_HEADER, "server_packet_mark masked set with all-bits mask replaces the whole mark", self.SET_MARK, 0xFFFFFFFF,
            self.SET_MARK)
        self._add_masked_case(
            self.ECHO_HEADER, "server_packet_mark masked set with no-bits mask leaves the mark unchanged", self.SET_MARK,
            0x00000000, self.SEED_MARK)


class ServerPacketMarkZeroSeedTest(PacketMarkTest):
    """Exercise the server masked overload from a zero starting mark, so the
    selected bits are the only bits set afterward.
    """

    ECHO_HEADER = "X-Server-Packet-Mark"

    def __init__(self):
        super().__init__(seed_mark=0x00000000)

    def _configure(self, ts, server):
        super()._configure(ts, server)
        ts.Disk.records_config.update(
            {
                'proxy.config.net.sock_packet_mark_out': self.SEED_MARK,
                'proxy.config.net.sock_option_flag_out': SOCK_OPT_FLAG_PACKET_MARK,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|server_packet_mark',
            })
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, f'server_packet_mark.so'), ts)

    def run(self):
        self._add_masked_case(
            self.ECHO_HEADER, "server_packet_mark masked set from a zero starting mark sets only the selected bits", 0x0000000A,
            0x0000000F, 0x0000000A)


ClientPacketMarkTest().run()
ClientPacketMarkZeroSeedTest().run()
ServerPacketMarkTest().run()
ServerPacketMarkZeroSeedTest().run()
