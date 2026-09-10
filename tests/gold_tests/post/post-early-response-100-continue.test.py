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

Test.Setup.CopyAs('early_response_100c_origin.py', Test.RunDirectory)
Test.Setup.CopyAs('early_response_100c_client.py', Test.RunDirectory)


def AddScenario(name: str, description: str, client_args: str, tunnel_expected: bool | None, guard_expected: bool | None) -> tuple:
    '''Run the early response probe against its own ATS and origin.

    Each scenario gets a dedicated ATS and origin so that a failure is attributable to the
    scenario rather than to a connection left over from an earlier run.

    :param name: Short identifier used for the process and port names.
    :param description: Test run description.
    :param client_args: Extra arguments passed to the client script.
    :param tunnel_expected: Whether a request body tunnel should exist when the origin
        responds, or None not to assert.  Asserted against the debug output, so that a
        scenario proves which code path it exercised.
    :param guard_expected: Whether the owed body guard should fire, or None not to assert.
    :return: The test run and the origin process, for further assertions.
    '''
    port_name = f'{name}_origin_port'
    Test.GetTcpPort(port_name)
    origin_port = getattr(Test.Variables, port_name)

    origin = Test.Processes.Process(
        f'{name}-origin', f'{sys.executable} {Test.RunDirectory}/early_response_100c_origin.py {origin_port}')
    origin.Ready = When.PortOpen(origin_port)

    ts = Test.MakeATSProcess(f'ts-{name}', select_ports=True, enable_cache=False)
    ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{origin_port}/')
    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http_tproxy|http_hdrs',
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
            'proxy.config.exec_thread.autoconfig': 0,
            'proxy.config.http.server_session_sharing.pool': 'global',
        })

    tr = Test.AddTestRun(description)
    tr.Processes.Default.Command = (
        f'{sys.executable} {Test.RunDirectory}/early_response_100c_client.py {ts.Variables.port} {client_args}')
    tr.Processes.Default.StartBefore(origin)
    tr.Processes.Default.StartBefore(ts)
    tr.Processes.Default.ReturnCode = 0
    tr.StillRunningAfter = ts
    tr.StillRunningAfter = origin

    # Prove which path ran.  "Tunnel alive" is logged only when a request body tunnel is
    # active as the origin response is parsed, so this distinguishes the no-tunnel trigger
    # from the path the earlier fix already covered.
    if tunnel_expected is True:
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            'Tunnel alive', 'A request body tunnel should be active in this scenario.')
    elif tunnel_expected is False:
        ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            'Tunnel alive', 'No request body tunnel should exist in this scenario.')

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
        tunnel_expected: bool | None,
        guard_expected: bool | None,
        early_status: str,
        post_target: str = '/early') -> None:
    '''Assert that a POST whose body is withheld never poisons the origin connection.

    :param name: Short identifier used for the process and port names.
    :param description: Test run description.
    :param client_args: Extra arguments passed to the client script.
    :param tunnel_expected: Whether a request body tunnel should exist.
    :param guard_expected: Whether the owed body guard should fire.
    :param early_status: The status line the origin's early response must be delivered with.
        Asserted so that an implementation which answered with a synthesized error instead
        of forwarding the origin's response cannot satisfy this scenario.
    :param post_target: The path the POST is sent to, which selects the origin's behavior.
    '''
    tr, origin = AddScenario(name, description, client_args, tunnel_expected, guard_expected)

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
    # The assertions above pass whether the connection was withheld from the pool or simply
    # never offered to the probe for some unrelated reason, because an origin whose drain
    # blocks to its timeout prints a zero-byte drain either way.  This is the direct inverse
    # of what the reuse scenario asserts, and it is what distinguishes the two.
    origin.Streams.stdout += Testers.ExcludesExpression(
        r'REQUEST 2 ', 'The origin connection must not be reused for a following request.')
    # The origin's own early response must be what reaches the client.  Without this an
    # implementation that answered every early-response POST with a synthesized error and a
    # close would satisfy every other assertion here.
    tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
        f'EARLY-POST STATUS HTTP/1.1 {early_status}', "The origin's early response must reach the client unchanged.")
    # The client connection must not be offered for reuse either, because the client
    # declared a body it never finished sending.  That is decided by do_drain_request_body()
    # rather than by the change here, so this locks in behavior this change must not
    # disturb rather than proving anything about the new guard.
    tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
        r'EARLY-POST CLIENT-CLOSE True', 'The client connection must not be offered for reuse.')
    # Normal traffic is not collateral damage.  This is not a regression oracle on its own,
    # because ATS retries the probe on a fresh connection even on a broken build.
    tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
        r'PROBE STATUS HTTP/1\.1 200 OK', 'The following request must still be served.')


# The regression case.  Expect: 100-continue is forwarded to the origin and the body is
# withheld until a 100 arrives, so no request body tunnel exists when the early response
# comes back.  The decision to close must not depend on a live tunnel.
AddWithheldBodyScenario(
    'expect',
    'POST with Expect 100-continue answered early, then a following request',
    '--expect-100-continue',
    False,
    True,
    early_status='420 Be Calm')

# The same withheld body without the Expect header, where a request body tunnel does
# exist.  Keeping it on the same build means a failure in the scenario above is
# attributable to the header rather than to the harness.
#
# This branch behaves differently from 10.x here, and the assertions follow the branch.
# 10.x defers a non-1xx early response behind the unfinished body, so its client receives
# nothing.  On 9.2.x the tunnel-alive path aborts the tunnel and forwards the response
# immediately, so the client does get the origin's early status and a close.  The guard is
# asserted on this path too, because on 9.2.x it is what decides the origin close: the
# NO_KEEPALIVE set alongside the tunnel abort is recomputed from the response headers
# afterwards, which is the defect the earlier fix on this branch addressed.
AddWithheldBodyScenario(
    'plain', 'POST answered early with no Expect header, then a following request', '', True, True, early_status='420 Be Calm')

# A 1xx that is neither 100 nor 103.  ATS dispatches those as final responses, so the
# declared body is never coming and the connection must not be pooled.  Without this the
# exemption could be widened back to the whole 1xx range and nothing would fail.
AddWithheldBodyScenario(
    'interim',
    'POST answered early with a 199, then a following request',
    '--expect-100-continue --path /interim',
    False,
    True,
    early_status='199 Hold On',
    post_target='/interim')

# The other direction, and the one a stuck flag would break: an ordinary
# Expect: 100-continue POST whose body is completed leaves the origin owed nothing, so its
# connection must still be pooled and reused.  Without this, closing every origin
# connection unconditionally would pass the scenarios above.
reuse_tr, reuse_origin = AddScenario(
    'reuse', 'POST with Expect 100-continue whose body completes, then a following request', '--complete-body --path /complete',
    None, False)
reuse_origin.Streams.stdout += Testers.ContainsExpression(
    r'SAW-EXPECT 1', 'ATS must forward Expect to the origin rather than answering it locally.')
reuse_origin.Streams.stdout += Testers.ContainsExpression(
    r'BODY-COMPLETE 16 of 16', 'The whole declared body must reach the origin.')
reuse_origin.Streams.stdout += Testers.ContainsExpression(
    r'REQUEST 2 GET /probe', 'The origin connection must be reused for the following request.')
reuse_tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    r'COMPLETE-POST STATUS HTTP/1\.1 200 OK', 'The completed POST must be answered normally.')

# The other half of the exemption.  HttpTransact::HandleResponse() calls
# initialize_state_variables_from_response() before handle_response_from_server() routes a
# 103 to handle_100_continue_response(), so at the instant the guard evaluates a 103 the
# debt is set and no request body tunnel exists, exactly as for a 100.  This pins the
# exempt set from the narrowing side: the 199 scenario above fails if the set is widened
# past 100 and 103, and this one fails if it is narrowed to 100 alone.
#
# The oracle here is the guard assertion, not the reuse.  Narrowing the set to 100 was
# tried, and the connection was still reused, because by the time the final 200 arrives the
# body has completed and the recompute restores keep_alive; only the spurious guard firing
# on the 103 shows up.  The reuse and body assertions are kept as a check that the
# exemption does not break the exchange itself.
#
# The origin sends a 103 and no 100.  ATS forwards any interim response as the go-ahead, so
# the body is still released.  An origin that sent a 100 after the 103 would crash a debug
# build of this branch on `ink_assert(!new_hdr->valid())` in
# HttpTransactHeaders::copy_header_fields(), because handle_100_continue_response() calls
# build_response_copy() into an hdr_info.client_response that the first interim response
# already filled in.  That reproduces on this branch with none of this change applied, and
# master carries the same code, so it is a separate pre-existing defect and not
# something this test should be shaped around beyond avoiding it.
hints_tr, hints_origin = AddScenario(
    'hints', 'POST with Expect 100-continue answered 103 before the go-ahead, then a following request',
    '--complete-body --path /earlyhints', None, False)
hints_tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    r'COMPLETE-POST INTERIM HTTP/1\.1 103', 'The 103 must be forwarded to the client.')
hints_origin.Streams.stdout += Testers.ContainsExpression(
    r'BODY-COMPLETE 16 of 16', 'The whole declared body must still reach the origin after a 103.')
hints_origin.Streams.stdout += Testers.ContainsExpression(
    r'REQUEST 2 GET /probe', 'The exemption must not cost this connection its reuse.')
hints_tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    r'COMPLETE-POST STATUS HTTP/1\.1 200 OK', 'The POST must be answered normally after a 103.')
