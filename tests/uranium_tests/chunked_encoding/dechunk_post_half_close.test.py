'''
Regression test: a nested half-close must not leave net_write_io on a cleared write VIO.
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
from ports import get_port

Test.Summary = __doc__
# Each method drives its own ATS, so a crash in one does not mask the other.
Test.ContinueOnFail = True

# Larger than the client socket buffers, yet read by ATS in one 32KB block.
BODY_SIZE = 24000
# The last-chunk must arrive in its own read while the client still has a backlog;
# sent with the body, it is parsed together with body bytes and the crash path is skipped.
LAST_CHUNK_HOLD = 0.5
CLIENT_STALL = 1.0


class DechunkHalfCloseTest:
    '''Drive a dechunked response whose last-chunk is parsed from a client WRITE_READY.

    The origin sends the whole body in one read, then the last-chunk on its own.
    The client stalls with a small receive buffer, so the last-chunk arrives while
    the user-agent consumer still has a backlog. With water_mark 1 the chunked
    throttle parks it unparsed. When the client drains, net_write_io signals
    WRITE_READY, and HttpTunnel::consumer_handler synthesizes a READ_READY that
    parses the last-chunk. That emits no body bytes, so local_finish_all sees
    nbytes == ndone and delivers WRITE_COMPLETE to tunnel_handler_ua on the same
    stack. For a POST on a closing connection that half-closes the client, and
    do_io_shutdown(IO_SHUTDOWN_WRITE) clears the write VIO net_write_io is
    still running on.

    GET takes a full do_io_close instead of the half-close, so it is the control.
    '''

    def __init__(self, method, expect_half_close):
        self.method = method
        self.name = f"dechunk-{method.lower()}-half-close"
        self.expect_half_close = expect_half_close
        self.setupOrigin()
        self.setupTS()

    def setupOrigin(self):
        script = os.path.join(Test.TestDirectory, "dechunk_post_half_close_origin.py")
        self.server = Test.Processes.Process(f"{self.name}-server")
        self.port = get_port(self.server, "Port")
        self.server.Command = f"python3 {script} {self.port} --body-size {BODY_SIZE} --hold {LAST_CHUNK_HOLD}"
        self.server.Ready = When.PortOpen(self.port)
        self.server.ReturnCode = Any(None, 0, -2, -9, -15)

    def setupTS(self):
        self.ts = Test.MakeATSProcess(f"ts-{self.name}")
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http_tunnel|http_cs",
                # Dechunk for the client, which also closes the connection.
                "proxy.config.http.chunking_enabled": 0,
                # Any backlog trips the chunked throttle.
                "proxy.config.http.default_buffer_water_mark": 1,
                # One 32KB block, so the whole body is read at once.
                "proxy.config.http.default_buffer_size": 8,
                # Keep the client socket small enough that the body backs up.
                "proxy.config.net.sock_send_buffer_size_in": 4096,
            })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.port}/")

        if self.expect_half_close:
            self.ts.Disk.traffic_out.Content = Testers.ContainsExpression(
                "session half close", "the POST must take the client half-close path")
        else:
            self.ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
                "session half close", "the GET must not take the client half-close path")

    def run(self):
        client = os.path.join(Test.TestDirectory, "dechunk_post_half_close_client.py")
        tr = Test.AddTestRun(f"{self.name}: dechunked response with a withheld last-chunk")
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.Command = f"python3 {client} {self.ts.Variables.port} --method {self.method} --stall {CLIENT_STALL} --sip 512 --interval 0.02"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.TimeOut = 90
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("status: HTTP/1.1 200", "the response must be served")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            f"body bytes: {BODY_SIZE}", "the full dechunked body must reach the client")
        tr.StillRunningAfter = self.ts


DechunkHalfCloseTest("POST", expect_half_close=True).run()
DechunkHalfCloseTest("GET", expect_half_close=False).run()
