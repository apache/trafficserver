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
Verify TSHttpTxnClientPacketMarkSet sets the client-side firewall mark to the
supplied value, using a test plugin that reads the applied mark back off the
client socket.
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
    Condition(_can_set_so_mark, "Setting SO_MARK requires Linux with CAP_NET_ADMIN or CAP_NET_RAW", True),
)


class ClientPacketMarkTest:
    """Drive TSHttpTxnClientPacketMarkSet through a test plugin and assert on the
    firewall mark read back off the client socket.

    The starting mark is seeded per process via
    proxy.config.net.sock_packet_mark_in, applied at accept time.
    """

    # Value the plugin sets; the mark is expected to become exactly this.
    SET_MARK = 0x0000000A

    def __init__(self):
        self._server = self._make_server()
        self._ts = self._make_ats("ts", seed_mark=0x0000FF00)

    def _make_server(self) -> 'Process':
        server = Test.MakeOriginServer("server")
        request_header = {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        server.addResponse("sessionlog.json", request_header, response_header)
        return server

    def _make_ats(self, name: str, seed_mark: int) -> 'Process':
        ts = Test.MakeATSProcess(name, enable_cache=False)
        ts.Disk.records_config.update(
            {
                'proxy.config.net.sock_packet_mark_in': seed_mark,
                'proxy.config.net.sock_option_flag_in': 0x11,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|client_packet_mark',
                'proxy.config.url_remap.remap_required': 0,
                # Keep ATS running as the invoking user inside sudo (no privilege drop).
                'proxy.config.admin.user_id': '#-1',
            })
        ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self._server.Variables.Port}")
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'client_packet_mark.so'), ts)
        return ts

    def run(self):
        # The mark is set to the supplied value, regardless of the seeded
        # starting mark.
        tr = Test.AddTestRun("TSHttpTxnClientPacketMarkSet sets the mark")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.MakeCurlCommand(
            f'--verbose --ipv4 --header "X-Set-Mark: 0x{self.SET_MARK:08x}" http://localhost:{self._ts.Variables.port}/',
            ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f"X-Client-Packet-Mark: 0x{self.SET_MARK:08x}", f"Observed client packet mark should be 0x{self.SET_MARK:08x}")


ClientPacketMarkTest().run()
