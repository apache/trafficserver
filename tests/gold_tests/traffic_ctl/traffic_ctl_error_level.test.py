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

import json
import os
import shlex
import sys

Test.Summary = '''
traffic_ctl --error-level: the exit status of a server error follows the
severity of its annotations, for every command and output format.

An annotation without a severity counts as an error, so the default level
(error) keeps exit 2 for every handler failure that does not say otherwise.
The drain handlers report "already in that state" as a warning, which exits 0
unless --error-level is warn or lower. Errors that did not come from a method
handler always exit 2.

The second half talks to a stand-in JSONRPC node, to show traffic_ctl
severities no real handler sends: none at all (an older server), out of range,
not a number, and a low one on an error that is not a handler failure.
'''

Test.ContinueOnFail = True

# RECA_READ_ONLY in src/records/RecordsConfig.cc: "config set" on it fails and its annotation carries no severity.
READ_ONLY_RECORD = 'proxy.config.thread.max_heartbeat_mseconds'

ts = Test.MakeATSProcess('ts')
started = set()


def run(description: str, command: str, return_code: int, process):
    '''A traffic_ctl run against process, started by the first run that needs it.'''
    tr = Test.AddTestRun(description)
    tr.Processes.Default.Command = command
    tr.Processes.Default.Env = ts.Env
    tr.Processes.Default.ReturnCode = return_code
    if process.Name not in started:
        tr.Processes.Default.StartBefore(process)
        started.add(process.Name)
    tr.StillRunningAfter = process
    return tr


def on_ts(description: str, command: str, return_code: int):
    return run(description, command, return_code, ts)


# ---------------------------------------------------------------------------------------------------------------------
# Against traffic_server
# ---------------------------------------------------------------------------------------------------------------------

on_ts('drain', 'traffic_ctl server drain', 0)

tr = on_ts('drain again: a warning is below the default level', 'traffic_ctl server drain', 0)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    r'- \[\d+\] Warn: Server already draining\.', 'the annotation is printed with its severity')

on_ts('drain again, --error-level warn', 'traffic_ctl --error-level warn server drain', 2)
tr = on_ts('drain again, level given after the command, alias and case', 'traffic_ctl server drain --error-level WARNING', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('Warn: Server already draining', 'the server was asked')
tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression('Unknown error level', 'the level was accepted')

tr = on_ts('drain again, json output', 'traffic_ctl -f json server drain', 0)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'"severity": "?4"?', 'the severity is on the wire')

on_ts('drain again, json output, --error-level warn', 'traffic_ctl -f json --error-level warn server drain', 2)

on_ts('undo drain', 'traffic_ctl server drain --undo', 0)
tr = on_ts('undo again: a warning', 'traffic_ctl server drain --undo', 0)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    r'- \[\d+\] Warn: Server is not draining\.', 'the annotation is printed with its severity')
on_ts('undo again, --error-level warn', 'traffic_ctl --error-level warn server drain --undo', 2)

tr = on_ts('set a read-only record: no severity counts as an error', f'traffic_ctl config set {READ_ONLY_RECORD} 999', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    r'- \[2009\] [^:]*read', 'printed as before, without a severity label')
on_ts('set a read-only record, --error-level fatal', f'traffic_ctl --error-level fatal config set {READ_ONLY_RECORD} 999', 0)
on_ts('set a read-only record, json output', f'traffic_ctl -f json config set {READ_ONLY_RECORD} 999', 2)

on_ts('unknown method', 'traffic_ctl rpc invoke nonexistent_rpc_method', 2)
tr = on_ts('unknown method, --error-level emergency', 'traffic_ctl --error-level emergency rpc invoke nonexistent_rpc_method', 2)
tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression('Unknown error level', 'the level was accepted')
on_ts('unknown method, json output', 'traffic_ctl -f json rpc invoke nonexistent_rpc_method', 2)

on_ts('success, --error-level diag', 'traffic_ctl --error-level diag config get proxy.config.http.server_ports', 0)

tr = on_ts('unknown level', 'traffic_ctl --error-level bogus config get proxy.config.http.server_ports', 2)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression('Unknown error level: bogus', 'the bad value is named')

# There is no short form: a literal "-e" is left to the command, here as the value of a record.
on_ts('-e is not an option', 'traffic_ctl config set proxy.config.diags.debug.tags -e', 0)
tr = on_ts('-e was set as the value', 'traffic_ctl config get proxy.config.diags.debug.tags', 0)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'proxy.config.diags.debug.tags: -e$', 'the record holds "-e"')

# Given more than once, in any spelling, the option is refused: no value silently overrides another one.
for twice in ('--error-level bogus --error-level warn', '--error-level=bogus --error-level warn',
              '--error-level warn --error-level=bogus', '--error-level=warn --error-level=fatal'):
    tr = on_ts(f'level given twice: {twice}', f'traffic_ctl {twice} config get proxy.config.http.server_ports', 64)
    tr.Processes.Default.Streams.All = Testers.ContainsExpression(
        'at most one argument expected by --error-level', 'the repetition is refused')

tr = on_ts('level given without a value', 'traffic_ctl config get proxy.config.http.server_ports --error-level', 2)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression('--error-level needs a value', 'not the default')

# ---------------------------------------------------------------------------------------------------------------------
# Against a stand-in JSONRPC node
# ---------------------------------------------------------------------------------------------------------------------


def execution_error(*entries: dict) -> dict:
    return {'error': {'code': 9, 'message': 'Error during execution', 'data': list(entries)}}


def stub(name: str, replies: dict):
    directory = os.path.join(Test.RunDirectory, name)
    process = Test.Processes.Process(
        name,
        f'{sys.executable} {os.path.join(Test.TestDirectory, "jsonrpc_error_stub.py")} {directory} {shlex.quote(json.dumps(replies))}'
    )
    process.Ready = When.FileExists(os.path.join(directory, 'ready'))
    return process, f'traffic_ctl --run-root={os.path.join(directory, "runroot.yaml")}'


node, ctl = stub(
    'stub', {
        'stub_old_server': execution_error({
            'code': 3000,
            'message': 'sent without severity'
        }),
        'stub_warn': execution_error({
            'code': 3000,
            'severity': 4,
            'message': 'already in that state'
        }),
        'stub_out_of_range': execution_error({
            'code': 1,
            'severity': 256,
            'message': 'out of range'
        }),
        'stub_not_a_number': execution_error({
            'code': 1,
            'severity': 'warn',
            'message': 'not a number'
        }),
        'stub_quoted_number': execution_error({
            'code': 1,
            'severity': '0',
            'message': 'quoted'
        }),
        'stub_diag': execution_error({
            'code': 1,
            'severity': 0,
            'message': 'diag entry'
        }),
        'stub_null_entry': execution_error(None),
        'stub_unauthorized':
            {
                'error':
                    {
                        'code': 10,
                        'message': 'Unauthorized action',
                        'data': [{
                            'code': 1,
                            'severity': 4,
                            'message': 'Denied privileged API access'
                        }]
                    }
            },
        'admin_config_reload': execution_error({
            'code': 1,
            'severity': 4,
            'message': 'reload warning'
        }),
        'get_reload_config_status': execution_error({
            'code': 1,
            'message': 'status failure'
        }),
    })


def on_stub(description: str, command: str, return_code: int, stub_process=None):
    return run(description, command, return_code, node if stub_process is None else stub_process)


tr = on_stub('older server, no severity: an error', f'{ctl} rpc invoke stub_old_server', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'- \[3000\] sent without severity', 'printed without a label')
on_stub('older server, no severity, --error-level fatal', f'{ctl} --error-level fatal rpc invoke stub_old_server', 0)

on_stub('warning', f'{ctl} rpc invoke stub_warn', 0)
on_stub('warning, --error-level warn', f'{ctl} --error-level warn rpc invoke stub_warn', 2)

tr = on_stub('severity above emergency does not wrap', f'{ctl} --error-level emergency rpc invoke stub_out_of_range', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'Severity\(256\): out of range', 'the raw value is shown')
tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression('Unknown error level', 'the level was accepted')

tr = on_stub('severity that is not a number always fails', f'{ctl} --error-level emergency rpc invoke stub_not_a_number', 2)
tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression('Unknown error level', 'the level was accepted')
tr = on_stub('a quoted number is not a severity', f'{ctl} --error-level emergency rpc invoke stub_quoted_number', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'Severity\(invalid\): quoted', 'shown as unreadable')
tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression('Unknown error level', 'the level was accepted')

on_stub('severity 0 is below the default level', f'{ctl} rpc invoke stub_diag', 0)
tr = on_stub('severity 0, --error-level diag', f'{ctl} --error-level diag rpc invoke stub_diag', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'- \[1\] Diag: diag entry', 'labelled Diag')

tr = on_stub('json output prints the reply as received', f'{ctl} -f json rpc invoke stub_null_entry', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'"data": \[null\]', 'the null entry is not rewritten')
on_stub('an entry that is not a map always fails', f'{ctl} --error-level emergency rpc invoke stub_null_entry', 2)
on_stub('unauthorized always fails, whatever its severity', f'{ctl} --error-level emergency rpc invoke stub_unauthorized', 2)

tr = on_stub('config reload: a warning is graded like any other command', f'{ctl} config reload', 0)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('Warn: reload warning', 'the warning is printed')
tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression('Error found', 'no second error report')
on_stub('config reload, --error-level warn', f'{ctl} --error-level warn config reload', 2)
on_stub('config reload, json output', f'{ctl} -f json config reload', 0)

tr = on_stub('config status: no severity is an error', f'{ctl} config status', 2)
tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression('Error found', 'no second error report')
on_stub('config status, --error-level fatal', f'{ctl} --error-level fatal config status', 0)

# The reload is refused (exit 2), then the status fetch fails with a warning: the warning must not clear the exit status.
token_node, token_ctl = stub(
    'stub_token', {
        'admin_config_reload': {
            'result': {
                'errors': [{
                    'code': 6002,
                    'message': "Token 't1' already exists."
                }]
            }
        },
        'get_reload_config_status': execution_error({
            'code': 1,
            'severity': 4,
            'message': 'status warning'
        }),
    })
tr = on_stub('an earlier failure is kept', f'{token_ctl} config reload -s -t t1', 2, token_node)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("Token 't1' already in use", 'the reload was refused')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('Warn: status warning', 'the status fetch failed next')

# config reload --monitor stops when a status request fails, before it sees how the reload ended. An error at or above the
# level fails the command; below it the outcome is unknown (75).
RELOAD_SCHEDULED = {'result': {'token': 'm1', 'message': ['Reload task scheduled']}}
IN_PROGRESS = {'result': {'tasks': [{'config_token': 'm1', 'status': 'in_progress'}]}}
MONITOR = 'config reload -t m1 -m --initial-wait 0 --refresh-int 0.1'

first_node, first_ctl = stub(
    'stub_mon_first', {
        'admin_config_reload': RELOAD_SCHEDULED,
        'get_reload_config_status': execution_error({
            'code': 1,
            'severity': 4,
            'message': 'status warning'
        }),
    })
tr = on_stub('monitor, first poll warns: outcome unknown', f'{first_ctl} {MONITOR}', 75, first_node)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('Warn: status warning', 'the poll failed')
on_stub('monitor, first poll warns, --error-level warn', f'{first_ctl} --error-level warn {MONITOR}', 2, first_node)

# Each run polls twice: in progress, then the error.
later_node, later_ctl = stub(
    'stub_mon_later', {
        'admin_config_reload': RELOAD_SCHEDULED,
        'get_reload_config_status': [IN_PROGRESS, execution_error({
            'code': 1,
            'message': 'status failure'
        })],
    })
tr = on_stub('monitor, a later poll fails', f'{later_ctl} {MONITOR}', 2, later_node)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('status failure', 'the second poll failed')
on_stub(
    'monitor, a later poll fails, --error-level fatal: outcome unknown', f'{later_ctl} --error-level fatal {MONITOR}', 75,
    later_node)

# The reload request itself fails below the level: nothing was scheduled, so --monitor cannot report success.
refused_node, refused_ctl = stub(
    'stub_mon_refused', {
        'admin_config_reload': execution_error({
            'code': 1,
            'severity': 4,
            'message': 'reload warning'
        }),
    })
tr = on_stub('monitor, the reload request warns: not a success', f'{refused_ctl} {MONITOR}', 75, refused_node)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('Warn: reload warning', 'the request failed')
on_stub('monitor, the reload request warns, --error-level warn', f'{refused_ctl} --error-level warn {MONITOR}', 2, refused_node)
