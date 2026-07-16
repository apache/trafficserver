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

    This base holds the shared skeleton -- process setup, the common records, the
    curl-and-assert case runner. Each subclass supplies its plugin and echo
    header and extends _configure() (via super()) with the side-specific mark and
    flag records.
    """

    # Value the plugin sets; the mark is expected to become exactly this.
    SET_MARK = 0x0000000A
    # Seeded starting mark, distinct from SET_MARK so a no-op would be visible.
    SEED_MARK = 0x0000FF00

    # Bumped per instance so each side gets uniquely-numbered processes.
    _counter = 0

    def __init__(self):
        self._num = PacketMarkTest._counter
        PacketMarkTest._counter += 1
        self._server = self._make_server()
        self._ts = self._make_ats()
        self._configure(self._ts)
        self._started = False

    def _make_server(self):
        server = Test.MakeOriginServer(f"server{self._num}")
        request_header = {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        for _ in range(2):
            server.addResponse("sessionlog.json", request_header, response_header)
        return server

    def _make_ats(self):
        return Test.MakeATSProcess(f"ts{self._num}", enable_cache=False)

    def _configure(self, ts):
        # Records and remap shared by both sides. Subclasses override to add the
        # side-specific mark/flag records and load their plugin, calling super()
        # for these.
        ts.Disk.records_config.update(
            {
                'proxy.config.url_remap.remap_required': 0,
                # Keep ATS running as the invoking user inside sudo (no privilege drop).
                'proxy.config.admin.user_id': '#-1',
            })
        ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self._server.Variables.Port}")

    def _add_case(self, echo_header: str, description: str, set_header: str):
        # The mark is set to the supplied value, regardless of the seeded
        # starting mark. The set is driven by whichever request header the plugin
        # keys on; the observed mark is echoed into echo_header. The origin server
        # and ATS are started before the first case, independent of the order in
        # which cases are added.
        tr = Test.AddTestRun(description)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        if not self._started:
            tr.StillRunningAfter = self._server
            tr.StillRunningAfter = self._ts
            self._started = True
        tr.MakeCurlCommand(
            f'--verbose --ipv4 --header "{set_header}: 0x{self.SET_MARK:08x}" http://localhost:{self._ts.Variables.port}/',
            ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f"{echo_header}: 0x{self.SET_MARK:08x}", f"Observed packet mark should be 0x{self.SET_MARK:08x}")


class ClientPacketMarkTest(PacketMarkTest):
    """Exercise TSHttpTxnClientPacketMarkSet. The client mark is seeded on the
    inbound socket.
    """

    ECHO_HEADER = "X-Client-Packet-Mark"

    def _configure(self, ts):
        super()._configure(ts)
        ts.Disk.records_config.update(
            {
                'proxy.config.net.sock_packet_mark_in': self.SEED_MARK,
                'proxy.config.net.sock_option_flag_in': SOCK_OPT_FLAG_PACKET_MARK,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|client_packet_mark',
            })
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, f'client_packet_mark.so'), ts)

    def run(self):
        self._add_case(self.ECHO_HEADER, "client_packet_mark sets the client-side mark on the live connection", "X-Set-Mark")


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

    def _configure(self, ts):
        super()._configure(ts)
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


ClientPacketMarkTest().run()
ServerPacketMarkTest().run()
