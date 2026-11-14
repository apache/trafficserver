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
import re
import shlex
import sys
import time
from pathlib import Path
from tools.uranium.services import ATSFactory, ServiceFactory, ProcessService, CommandResult


def test_traffic_ctl_error_level(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Check severity-based exits against ATS and controlled JSON-RPC replies.

    :param ats_factory: Proxy factory.
    :param services: Stand-in RPC server factory.
    """
    ats = ats_factory.create("ts")
    ats.start()

    def run(command: str, return_code: int) -> CommandResult:
        """Execute one CLI request and preserve the full diagnostic output.

        :param command: Shell-style argument string, including the executable.
        :param return_code: Exact expected command status.
        """
        result = ats.run(*shlex.split(command))
        assert result.returncode == return_code, result.output
        return result

    def stub(name: str, replies: dict) -> tuple[ProcessService, str]:
        """Start a stand-in RPC node with deterministic method replies.

        :param name: Unique process and runroot name.
        :param replies: Method names mapped to JSON-RPC response payloads.
        """
        directory = ats.run_directory / name
        process = services.process(
            name, [sys.executable, Path(__file__).with_name("jsonrpc_error_stub.py"), directory, json.dumps(replies)]
        )
        process.start()
        deadline = time.monotonic() + 10
        while not (directory / "ready").exists():
            assert process.is_running, process.output
            assert time.monotonic() < deadline, process.output
            time.sleep(0.01)
        return process, f"traffic_ctl --run-root={shlex.quote(str(directory / 'runroot.yaml'))}"

    READ_ONLY_RECORD = 'proxy.config.thread.max_heartbeat_mseconds'
    run('traffic_ctl server drain', 0)
    tr = run('traffic_ctl server drain', 0)
    assert re.search('- \\[\\d+\\] Warn: Server already draining\\.', tr.stdout, re.MULTILINE), tr.output
    run('traffic_ctl --error-level warn server drain', 2)
    tr = run('traffic_ctl --error-level note server drain', 2)
    assert re.search('Warn: Server already draining', tr.stdout, re.MULTILINE), tr.output
    assert not re.search('Unknown error level', tr.stderr, re.MULTILINE), tr.output
    tr = run('traffic_ctl server drain --error-level WARNING', 2)
    assert re.search('Warn: Server already draining', tr.stdout, re.MULTILINE), tr.output
    assert not re.search('Unknown error level', tr.stderr, re.MULTILINE), tr.output
    tr = run('traffic_ctl -f json server drain', 0)
    assert re.search('"severity": "?4"?', tr.stdout, re.MULTILINE), tr.output
    run('traffic_ctl -f json --error-level warn server drain', 2)
    run('traffic_ctl server drain --undo', 0)
    tr = run('traffic_ctl server drain --undo', 0)
    assert re.search('- \\[\\d+\\] Warn: Server is not draining\\.', tr.stdout, re.MULTILINE), tr.output
    run('traffic_ctl --error-level warn server drain --undo', 2)
    tr = run(f'traffic_ctl config set {READ_ONLY_RECORD} 999', 2)
    assert re.search('- \\[2009\\] [^:]*read', tr.stdout, re.MULTILINE), tr.output
    run(f'traffic_ctl --error-level fatal config set {READ_ONLY_RECORD} 999', 0)
    run(f'traffic_ctl -f json config set {READ_ONLY_RECORD} 999', 2)
    run('traffic_ctl rpc invoke nonexistent_rpc_method', 2)
    tr = run('traffic_ctl --error-level emergency rpc invoke nonexistent_rpc_method', 2)
    assert not re.search('Unknown error level', tr.stderr, re.MULTILINE), tr.output
    run('traffic_ctl -f json rpc invoke nonexistent_rpc_method', 2)
    run('traffic_ctl --error-level diag config get proxy.config.http.server_ports', 0)
    tr = run('traffic_ctl --error-level bogus config get proxy.config.http.server_ports', 2)
    assert re.search('Unknown error level: bogus', tr.stderr, re.MULTILINE), tr.output
    run('traffic_ctl config set proxy.config.diags.debug.tags -e', 0)
    tr = run('traffic_ctl config get proxy.config.diags.debug.tags', 0)
    assert re.search('proxy.config.diags.debug.tags: -e$', tr.stdout, re.MULTILINE), tr.output
    for twice in (
        '--error-level bogus --error-level warn',
        '--error-level=bogus --error-level warn',
        '--error-level warn --error-level=bogus',
        '--error-level=warn --error-level=fatal',
    ):
        tr = run(f'traffic_ctl {twice} config get proxy.config.http.server_ports', 64)
        assert re.search('at most one argument expected by --error-level', tr.output, re.MULTILINE), tr.output
    tr = run('traffic_ctl config get proxy.config.http.server_ports --error-level', 2)
    assert re.search('--error-level needs a value', tr.stderr, re.MULTILINE), tr.output

    def execution_error(*entries: dict | None) -> dict:
        """Build an execution error response.

        :param entries: Annotation payloads, including malformed null entries.
        """
        return {'error': {'code': 9, 'message': 'Error during execution', 'data': list(entries)}}

    node, ctl = stub(
        'stub',
        {
            'stub_old_server': execution_error({'code': 3000, 'message': 'sent without severity'}),
            'stub_warn': execution_error({'code': 3000, 'severity': 4, 'message': 'already in that state'}),
            'stub_out_of_range': execution_error({'code': 1, 'severity': 256, 'message': 'out of range'}),
            'stub_not_a_number': execution_error({'code': 1, 'severity': 'warn', 'message': 'not a number'}),
            'stub_quoted_number': execution_error({'code': 1, 'severity': '0', 'message': 'quoted'}),
            'stub_diag': execution_error({'code': 1, 'severity': 0, 'message': 'diag entry'}),
            'stub_null_entry': execution_error(None),
            'stub_unauthorized': {
                'error': {
                    'code': 10,
                    'message': 'Unauthorized action',
                    'data': [{'code': 1, 'severity': 4, 'message': 'Denied privileged API access'}],
                }
            },
            'admin_config_reload': execution_error({'code': 1, 'severity': 4, 'message': 'reload warning'}),
            'get_reload_config_status': execution_error({'code': 1, 'message': 'status failure'}),
        },
    )
    tr = run(f'{ctl} rpc invoke stub_old_server', 2)
    assert re.search('- \\[3000\\] sent without severity', tr.stdout, re.MULTILINE), tr.output
    run(f'{ctl} --error-level fatal rpc invoke stub_old_server', 0)
    run(f'{ctl} rpc invoke stub_warn', 0)
    run(f'{ctl} --error-level warn rpc invoke stub_warn', 2)
    tr = run(f'{ctl} --error-level emergency rpc invoke stub_out_of_range', 2)
    assert re.search('Severity\\(256\\): out of range', tr.stdout, re.MULTILINE), tr.output
    assert not re.search('Unknown error level', tr.stderr, re.MULTILINE), tr.output
    tr = run(f'{ctl} --error-level emergency rpc invoke stub_not_a_number', 2)
    assert not re.search('Unknown error level', tr.stderr, re.MULTILINE), tr.output
    tr = run(f'{ctl} --error-level emergency rpc invoke stub_quoted_number', 2)
    assert re.search('Severity\\(invalid\\): quoted', tr.stdout, re.MULTILINE), tr.output
    assert not re.search('Unknown error level', tr.stderr, re.MULTILINE), tr.output
    run(f'{ctl} rpc invoke stub_diag', 0)
    tr = run(f'{ctl} --error-level diag rpc invoke stub_diag', 2)
    assert re.search('- \\[1\\] Diag: diag entry', tr.stdout, re.MULTILINE), tr.output
    tr = run(f'{ctl} -f json rpc invoke stub_null_entry', 2)
    assert re.search('"data": \\[null\\]', tr.stdout, re.MULTILINE), tr.output
    run(f'{ctl} --error-level emergency rpc invoke stub_null_entry', 2)
    run(f'{ctl} --error-level emergency rpc invoke stub_unauthorized', 2)
    tr = run(f'{ctl} config reload', 0)
    assert re.search('Warn: reload warning', tr.stdout, re.MULTILINE), tr.output
    assert not re.search('Error found', tr.stderr, re.MULTILINE), tr.output
    run(f'{ctl} --error-level warn config reload', 2)
    run(f'{ctl} -f json config reload', 0)
    tr = run(f'{ctl} config status', 2)
    assert not re.search('Error found', tr.stderr, re.MULTILINE), tr.output
    run(f'{ctl} --error-level fatal config status', 0)
    token_node, token_ctl = stub(
        'stub_token',
        {
            'admin_config_reload': {'result': {'errors': [{'code': 6002, 'message': "Token 't1' already exists."}]}},
            'get_reload_config_status': execution_error({'code': 1, 'severity': 4, 'message': 'status warning'}),
        },
    )
    tr = run(f'{token_ctl} config reload -s -t t1', 2)
    assert re.search("Token 't1' already in use", tr.stdout, re.MULTILINE), tr.output
    assert re.search('Warn: status warning', tr.stdout, re.MULTILINE), tr.output
    DEBUG_SET = {'result': {'record': [{'record_name': 'proxy.config.diags.debug.tags'}]}}
    UNAUTHORIZED = {
        'error': {'code': 10, 'message': 'Unauthorized action', 'data': [{'code': 2, 'message': 'Denied privileged API access'}]}
    }
    LOOKUP_WARNING = execution_error({'code': 1, 'severity': 4, 'message': 'lookup warning'})
    APPEND = 'server debug enable --tags http --append'
    denied_node, denied_ctl = stub('stub_debug', {'admin_lookup_records': UNAUTHORIZED, 'admin_config_set_records': DEBUG_SET})
    warned_node, warned_ctl = stub(
        'stub_debug_warn', {'admin_lookup_records': LOOKUP_WARNING, 'admin_config_set_records': DEBUG_SET}
    )
    DEBUG_LOOKUP_FAILURES = [
        ('the tag lookup is refused', denied_node, f'{denied_ctl} {APPEND}', 2, 'Denied privileged API access'),
        ('the tag lookup warns: not done', warned_node, f'{warned_ctl} {APPEND}', 75, 'lookup warning'),
        ('the tag lookup warns, --error-level warn', warned_node, f'{warned_ctl} --error-level warn {APPEND}', 2, 'lookup warning'),
    ]
    for what, process, command, code, why in DEBUG_LOOKUP_FAILURES:
        tr = run(command, code)
        assert re.search(why, tr.stdout, re.MULTILINE), tr.output
        assert not re.search('TS Runtime debug set', tr.stdout, re.MULTILINE), tr.output
        assert not re.search('Error found', tr.stderr, re.MULTILINE), tr.output
    METRIC_OK = {
        'result': {'recordList': [{'record': {'record_name': 'proxy.process.stub', 'current_value': '5'}}], 'errorList': []}
    }
    reset_node, reset_ctl = stub('stub_reset', {'admin_lookup_records': LOOKUP_WARNING})
    RESET_STOPS = [
        ('the lookup warns: not done', f'{reset_ctl} config reset proxy.config.diags', 75),
        ('the lookup warns, --error-level warn', f'{reset_ctl} --error-level warn config reset proxy.config.diags', 2),
    ]
    for what, command, code in RESET_STOPS:
        tr = run(command, code)
        assert re.search('lookup warning', tr.stdout, re.MULTILINE), tr.output
        assert not re.search('Method not found', tr.stdout, re.MULTILINE), tr.output
        assert not re.search('Error found', tr.stderr, re.MULTILINE), tr.output
    metric_node, metric_ctl = stub('stub_metric', {'admin_lookup_records': [METRIC_OK, LOOKUP_WARNING]})
    tr = run(f'{metric_ctl} metric monitor proxy.process.stub -c 3 -i 1', 75)
    assert re.search('proxy.process.stub: 5', tr.stdout, re.MULTILINE), tr.output
    assert re.search('lookup warning', tr.stdout, re.MULTILINE), tr.output
    tr = run(f'{metric_ctl} --error-level warn metric monitor proxy.process.stub -c 3 -i 1', 2)
    assert not re.search('Error found', tr.stderr, re.MULTILINE), tr.output
    RELOAD_SCHEDULED = {'result': {'token': 'm1', 'message': ['Reload task scheduled']}}
    IN_PROGRESS = {'result': {'tasks': [{'config_token': 'm1', 'status': 'in_progress'}]}}
    MONITOR = 'config reload -t m1 -m --initial-wait 0 --refresh-int 0.1'
    first_node, first_ctl = stub(
        'stub_mon_first',
        {
            'admin_config_reload': RELOAD_SCHEDULED,
            'get_reload_config_status': execution_error({'code': 1, 'severity': 4, 'message': 'status warning'}),
        },
    )
    tr = run(f'{first_ctl} {MONITOR}', 75)
    assert re.search('Warn: status warning', tr.stdout, re.MULTILINE), tr.output
    run(f'{first_ctl} --error-level warn {MONITOR}', 2)
    later_node, later_ctl = stub(
        'stub_mon_later',
        {
            'admin_config_reload': RELOAD_SCHEDULED,
            'get_reload_config_status': [IN_PROGRESS, execution_error({'code': 1, 'message': 'status failure'})],
        },
    )
    tr = run(f'{later_ctl} {MONITOR}', 2)
    assert re.search('status failure', tr.stdout, re.MULTILINE), tr.output
    run(f'{later_ctl} --error-level fatal {MONITOR}', 75)
    refused_node, refused_ctl = stub(
        'stub_mon_refused', {'admin_config_reload': execution_error({'code': 1, 'severity': 4, 'message': 'reload warning'})}
    )
    tr = run(f'{refused_ctl} {MONITOR}', 75)
    assert re.search('Warn: reload warning', tr.stdout, re.MULTILINE), tr.output
    run(f'{refused_ctl} --error-level warn {MONITOR}', 2)
    details_node, details_ctl = stub(
        'stub_details',
        {
            'admin_config_reload': RELOAD_SCHEDULED,
            'get_reload_config_status': execution_error({'code': 1, 'severity': 4, 'message': 'status warning'}),
        },
    )
    SHOW_DETAILS = 'config reload -t m1 -s --initial-wait 0'
    tr = run(f'{details_ctl} {SHOW_DETAILS}', 75)
    assert re.search('Reload scheduled \\[m1\\]', tr.stdout, re.MULTILINE), tr.output
    assert re.search('Warn: status warning', tr.stdout, re.MULTILINE), tr.output
    assert not re.search('Error found', tr.stderr, re.MULTILINE), tr.output
    tr = run(f'{details_ctl} --error-level warn {SHOW_DETAILS}', 2)
    assert not re.search('Error found', tr.stderr, re.MULTILINE), tr.output
    tr = run(f'{refused_ctl} {SHOW_DETAILS}', 75)
    assert re.search('Warn: reload warning', tr.stdout, re.MULTILINE), tr.output
    assert not re.search('Reload scheduled', tr.stdout, re.MULTILINE), tr.output
    assert not re.search('Error found', tr.stderr, re.MULTILINE), tr.output
    run(f'{refused_ctl} --error-level warn {SHOW_DETAILS}', 2)
