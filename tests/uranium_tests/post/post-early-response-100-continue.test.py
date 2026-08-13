'''Verify an origin owed a request body is never handed the next request.
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

import sys

Test.Summary = '''
An origin that answers a POST before reading its body is still owed that body.  ATS must
not return such a connection to the session pool, or the next request written to it is
consumed by the origin as the leftover body instead of being served.  An origin that is
sent the whole body must still get its connection reused.
'''

Test.ContinueOnFail = True

Test.Setup.CopyAs('early_response_origin.py', Test.RunDirectory)
Test.Setup.CopyAs('early_response_client.py', Test.RunDirectory)


def AddScenario(name: str, description: str, client_args: str, guard_expected: bool | None) -> tuple:
    '''Run the early response probe against its own ATS and origin.

    Each scenario gets a dedicated ATS and origin so that a failure is attributable to the
    scenario rather than to a connection left over from an earlier run.

    :param name: Short identifier used for the process and port names.
    :param description: Test run description.
    :param client_args: Extra arguments passed to the client script.
    :param guard_expected: Whether the owed body guard should fire, or None not to assert.
    :return: The test run and the origin process, for further assertions.
    '''
    port_name = f'{name}_origin_port'
    Test.GetTcpPort(port_name)
    origin_port = getattr(Test.Variables, port_name)

    origin = Test.Processes.Process(
        f'{name}-origin', f'{sys.executable} {Test.RunDirectory}/early_response_origin.py {origin_port}')
    origin.Ready = When.PortOpen(origin_port)

    ts = Test.MakeATSProcess(f'ts-{name}', select_ports=True, enable_cache=False)
    ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{origin_port}/')
    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http_hdrs',
            # The scenario depends on ATS forwarding Expect to the origin rather than
            # answering it locally.  That is the default, but pin it so this keeps
            # exercising the forwarded Expect path even if the default changes.
            'proxy.config.http.send_100_continue_response': 0,
            # One event thread and a single shared pool, so that the origin connection from
            # the first request is the one offered to the second.  With the default
            # per-thread pool neither the reuse asserted against below nor the reuse
            # asserted for can be provoked reliably, and the test would pass on a broken
            # build.
            'proxy.config.exec_thread.limit': 1,
            'proxy.config.exec_thread.autoconfig.enabled': 0,
            'proxy.config.http.server_session_sharing.pool': 'global',
        })

    tr = Test.AddTestRun(description)
    tr.Processes.Default.Command = (
        f'{sys.executable} {Test.RunDirectory}/early_response_client.py {ts.Variables.port} {client_args}')
    tr.Processes.Default.StartBefore(origin)
    tr.Processes.Default.StartBefore(ts)
    tr.Processes.Default.ReturnCode = 0
    tr.StillRunningAfter = ts
    tr.StillRunningAfter = origin

    # Assert directly on the decision the fix adds, so that a scenario proves the changed
    # code ran rather than only that the outcome happened to be right.
    guard_message = 'Origin is owed request body bytes'
    if guard_expected is True:
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            guard_message, 'The owed body guard should refuse to reuse these connections.')
    elif guard_expected is False:
        ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            guard_message, 'The owed body guard must not fire when the origin got the whole body.')

    return tr, origin


def AddWithheldBodyScenario(
        name: str,
        description: str,
        client_args: str,
        guard_expected: bool | None,
        post_target: str = '/early',
        early_status: str | None = None) -> None:
    '''Assert that a POST whose body is withheld never poisons the origin connection.

    :param name: Short identifier used for the process and port names.
    :param description: Test run description.
    :param client_args: Extra arguments passed to the client script.
    :param guard_expected: Whether the owed body guard should fire.
    :param post_target: The path the POST is sent to, which selects the origin's behavior.
    :param early_status: If given, the status line the client must receive for the POST.
    '''
    tr, origin = AddScenario(name, description, client_args, guard_expected)

    # Positive assertions first, so that an origin that never ran cannot satisfy the
    # negative one below by staying silent.
    origin.Streams.stdout += Testers.ContainsExpression(
        f'REQUEST 1 POST {post_target}', 'The origin must have received the early POST.')
    # The direct inverse of the broken behavior, which drains part of the declared body.
    origin.Streams.stdout += Testers.ContainsExpression(r'DRAINED 0 of 100000', 'Nothing may be drained as this POST body.')
    # Framing independent: the origin must never see a request line inside what it drains
    # as the earlier POST's body.
    origin.Streams.stdout += Testers.ExcludesExpression(
        r'CONSUMED-AS-BODY', 'No request may be consumed as the body of the earlier POST.')
    # The client connection must not be offered for reuse either, because the client
    # declared a body it never finished sending.  That is decided by do_drain_request_body()
    # rather than by the change here, so this locks in behavior this change must not
    # disturb rather than proving anything about the new guard.
    tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
        r'EARLY-POST CLIENT-CLOSE True', 'The client connection must not be offered for reuse.')
    if early_status is not None:
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            rf'EARLY-POST STATUS {early_status}', 'The early origin response must be forwarded to the client.')
    # Normal traffic is not collateral damage.  This is not a regression oracle on its own,
    # because ATS retries the probe on a fresh connection even on a broken build.
    tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
        r'PROBE STATUS HTTP/1\.1 200 OK', 'The following request must still be served.')


# The regression case.  Expect: 100-continue is forwarded to the origin and the body is
# withheld until a 100 arrives, so no request body tunnel exists when the early response
# comes back.  The decision to close must not depend on a live tunnel.
AddWithheldBodyScenario(
    'expect', 'POST with Expect 100-continue answered early, then a following request', '--expect-100-continue', True)

# The same withheld body without the Expect header, where a request body tunnel does
# exist.  Keeping it on the same build means a failure in the scenario above is
# attributable to the header rather than to the harness.
# The origin's early response ends the tunnel, and is forwarded to the client at once
# with the client connection closed.  The origin connection was still owed the rest of the
# body, so the guard must keep it out of the pool.
AddWithheldBodyScenario(
    'plain', 'POST answered early with no Expect header, then a following request', '', True, early_status=r'HTTP/1\.1 420')

# A 1xx that is neither 100 nor 103.  ATS dispatches those as final responses, so the
# declared body is never coming and the connection must not be pooled.  Without this the
# exemption could be widened back to the whole 1xx range and nothing would fail.
AddWithheldBodyScenario(
    'interim',
    'POST answered early with a 199, then a following request',
    '--expect-100-continue --path /interim',
    True,
    post_target='/interim')

# The other direction, and the one a stuck flag would break: an ordinary
# Expect: 100-continue POST whose body is completed leaves the origin owed nothing, so its
# connection must still be pooled and reused.  Without this, closing every origin
# connection unconditionally would pass the scenarios above.
reuse_tr, reuse_origin = AddScenario(
    'reuse', 'POST with Expect 100-continue whose body completes, then a following request', '--complete-body --path /complete',
    False)
reuse_origin.Streams.stdout += Testers.ContainsExpression(
    r'SAW-EXPECT 1', 'ATS must forward Expect to the origin rather than answering it locally.')
reuse_origin.Streams.stdout += Testers.ContainsExpression(
    r'BODY-COMPLETE 16 of 16', 'The whole declared body must reach the origin.')
reuse_origin.Streams.stdout += Testers.ContainsExpression(
    r'REQUEST 2 GET /probe', 'The origin connection must be reused for the following request.')
reuse_tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    r'COMPLETE-POST STATUS HTTP/1\.1 200 OK', 'The completed POST must be answered normally.')

# The 103 half of the exemption is deliberately not covered here.  ATS treats any forwarded
# interim response as the go-ahead and opens the request body tunnel in
# tunnel_handler_100_continue(), so an origin that sends 103 before its 100 never gets the
# body and the scenario cannot distinguish the exemption from its absence.
