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
from collections.abc import Sequence
from pathlib import Path
from tools.uranium.services import ATS, ATSFactory, ServiceFactory, ProcessService, CommandResult, wait_for_file_lines

DL_NOTE, DL_WARNING, DL_ERROR, DL_FATAL, DL_UNDEFINED = 3, 4, 5, 6, 9
NOTE = 'Inline configuration is NOT persisted'


def run(ats: ATS, command: str, return_code: int) -> CommandResult:
    """Run one shell command with the environment of @a ats.

    The exit status must come from the grade: traffic_ctl also exits 2 when it cannot decode a reply, so a report must
    have been printed and no error found.

    :param ats: Traffic Server whose environment and run root the command uses.
    :param command: Bash command line.
    :param return_code: Exact expected command status.
    """
    # Longer than the sleep plus the 30 s monitor timeout of MONITOR_TS, so a traffic_server reload that never ends shows the
    # monitor's own report.
    result = ats.run_shell(command, timeout=60)
    assert result.returncode == return_code, result.output
    assert 'Error found' not in result.stderr, result.output
    if ' config status' in command:
        report = r'"tasks"' if ' -f json ' in command else r'Reload \['
        assert has(result, report), result.output
    return result


def has(result: CommandResult, pattern: str) -> bool:
    """Return whether a line of the command's stdout matches.

    :param result: Completed command.
    :param pattern: Regular expression searched in stdout, line anchors per line.
    """
    return re.search(pattern, result.stdout, re.MULTILINE) is not None


def task(status: str, levels: Sequence[int | None] = (), sub_tasks: Sequence[dict] = (), token: str = 'r1') -> dict:
    """Build a reload task as get_reload_config_status reports it.

    :param status: Task state.
    :param levels: One log entry per level; a level of None is an entry sent without one.
    :param sub_tasks: Child tasks.
    :param token: Reload token.
    """
    logs = [{'text': f'entry {i}'} if level is None else {'level': level, 'text': f'entry {i}'} for i, level in enumerate(levels)]
    return {'config_token': token, 'status': status, 'description': 'stub task', 'logs': logs, 'sub_tasks': list(sub_tasks)}


def status_reply(*tasks: dict) -> dict:
    """Build a get_reload_config_status reply.

    :param tasks: Reload tasks, oldest first.
    """
    return {'result': {'tasks': list(tasks)}}


def test_reload_error_level(ats_factory: ATSFactory) -> None:
    """Grade reloads of a real traffic_server by --error-level.

    config reload --monitor and config status grade a finished reload by the most severe log entry of the task and its
    sub-tasks. The state of each task bounds its entries: fail and timeout count as at least an error, and the entries of
    a task that did not fail count as at most a warning, so at the default level the exit status follows the state.

    :param ats_factory: Proxy factory.
    """
    ats = ats_factory.create('ts')
    ats.cache_config.add_line('dest_domain=example.com ttl-in-cache=30d')
    ats.start()
    MONITOR_TS = '-m --initial-wait 0.5 --refresh-int 0.5 --timeout 30s'
    cache_config = shlex.quote(str(ats.config_directory / 'cache.config'))

    # A cache.config line with a malformed IP is discarded: the cache.config task logs an error and still ends success.
    tr = run(
        ats, f"echo 'dest_ip=999.1.1.1 action=never-cache' >> {cache_config} && sleep 2 && "
        f"traffic_ctl config reload -t bad_line {MONITOR_TS}", 0)
    assert has(tr, r'\d+/\d+ +success'), tr.output
    assert not has(tr, 'Details :'), tr.output
    tr = run(ats, 'traffic_ctl config status -t bad_line', 0)
    assert has(tr, r'\[Err\] +\[CacheControl\] not a valid IP address range'), tr.output
    # The error entry of a success counts as a warning.
    run(ats, 'traffic_ctl --error-level warn config status -t bad_line', 2)
    run(ats, 'traffic_ctl -f json --error-level warn config status -t bad_line', 2)

    # Put the line right: the next reload of cache.config is clean, apart from its notes.
    tr = run(
        ats, f"echo 'dest_domain=example.com ttl-in-cache=30d' > {cache_config} && sleep 2 && "
        f"traffic_ctl config reload -t clean {MONITOR_TS}", 0)
    assert not has(tr, 'Details :'), tr.output
    run(ats, 'traffic_ctl --error-level warn config status -t clean', 0)
    run(ats, 'traffic_ctl --error-level note config status -t clean', 2)
    # Entries hidden by --min-level are still graded.
    run(ats, 'traffic_ctl --error-level note config status -t clean --min-level error', 2)

    # A directory in place of cache.config cannot be read: the cache.config task fails.
    tr = run(
        ats, f"rm {cache_config} && mkdir {cache_config} && sleep 2 && "
        f"traffic_ctl config reload -t unreadable {MONITOR_TS}", 2)
    assert has(tr, r'\d+/\d+ +fail'), tr.output
    assert has(tr, 'Details : traffic_ctl config status -t unreadable'), tr.output
    run(ats, 'traffic_ctl config status -t unreadable', 2)
    run(ats, 'traffic_ctl --error-level fatal config status -t unreadable', 0)
    # Put cache.config back; the default level, given explicitly, still fails the reload.
    run(
        ats, f"rmdir {cache_config} && echo 'dest_domain=example.com ttl-in-cache=30d' > {cache_config} && "
        "traffic_ctl --error-level error config status -t unreadable", 2)

    # traffic_server reports the rejected line and the unreadable file in diags.log too.
    diagnostics = wait_for_file_lines(ats.diags_log, 'cache.config failed to load', 1)
    assert 'not a valid IP address range' in diagnostics, diagnostics


def test_reload_error_level_stub(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Grade reload task trees that no handler produces on demand, served by a stand-in JSON-RPC node.

    Entries without a severity are not graded, and a reload that is still running, or in a state traffic_ctl does not
    know, is not graded. A timeout below the level exits 75. config status grades the newest reload it shows.

    :param ats_factory: Proxy factory; traffic_ctl runs with the environment of its Traffic Server.
    :param services: Stand-in RPC server factory.
    """
    ats = ats_factory.create('ts')
    ats.start()
    nodes: dict[str, ProcessService] = {}

    def stub(name: str, replies: dict) -> str:
        """Start a stand-in RPC node and return the traffic_ctl command that talks to it.

        :param name: Unique process name, also the directory that holds its UNIX socket. Keep it short: macOS caps socket
            paths at 104 bytes.
        :param replies: Method names mapped to a JSON-RPC reply, or to a list of replies served in turn.
        """
        directory = ats.run_directory / name
        process = services.process(
            name,
            [sys.executable, Path(__file__).with_name('jsonrpc_error_stub.py'), directory,
             json.dumps(replies)])
        process.start()
        deadline = time.monotonic() + 10
        while not (directory / 'ready').exists():
            assert process.is_running, process.output
            assert time.monotonic() < deadline, process.output
            time.sleep(0.01)
        ctl = f"traffic_ctl --run-root={shlex.quote(str(directory / 'runroot.yaml'))}"
        nodes[ctl] = process
        return ctl

    def status_stub(name: str, *tasks: dict) -> str:
        """Start a stand-in RPC node that reports @a tasks.

        :param name: Unique process name.
        :param tasks: Reload tasks, oldest first.
        """
        return stub(name, {'get_reload_config_status': status_reply(*tasks)})

    def on_stub(ctl: str, args: str, return_code: int) -> CommandResult:
        """Run traffic_ctl against a stand-in node, which must still be running afterwards.

        :param ctl: traffic_ctl command that talks to the node.
        :param args: traffic_ctl arguments.
        :param return_code: Exact expected command status.
        """
        tr = run(ats, f'{ctl} {args}', return_code)
        assert nodes[ctl].is_running, nodes[ctl].output
        return tr

    # success with a warning two levels down: sub-task entries count, at any depth.
    ctl = status_stub('st_warn', task('success', [DL_NOTE], [task('success', [DL_NOTE], [task('success', [DL_WARNING])])]))
    on_stub(ctl, 'config status', 0)
    on_stub(ctl, '--error-level warn config status', 2)
    # Hidden by --min-level, still graded.
    on_stub(ctl, '--error-level warn config status --min-level error', 2)

    # success with a fatal entry: the entries of a task that succeeded count as at most a warning.
    ctl = status_stub('st_capped', task('success', [], [task('success', [DL_FATAL])]))
    on_stub(ctl, 'config status', 0)
    on_stub(ctl, '--error-level warn config status', 2)

    # fail without any entry: the state alone is an error, and fatal relaxes it.
    ctl = status_stub('st_bare_fail', task('fail'))
    tr = on_stub(ctl, 'config status', 2)
    assert has(tr, r'\[fail\]'), tr.output
    on_stub(ctl, '--error-level fatal config status', 0)

    # fail with a fatal entry in a sub-task that failed: fatal does not relax it.
    ctl = status_stub('st_fatal', task('fail', [DL_ERROR], [task('fail', [DL_FATAL])]))
    on_stub(ctl, '--error-level fatal config status', 2)
    on_stub(ctl, '--error-level alert config status', 0)

    # timeout whose only entry has no severity, like the reason the progress checker logs.
    ctl = status_stub('st_timeout', task('timeout', [DL_UNDEFINED]))
    on_stub(ctl, 'config status', 2)
    on_stub(ctl, '--error-level fatal config status', 75)

    # Entries without a severity, sent as level 9, without a level, or as plain strings (an older server), are not graded.
    unleveled = task('success', [DL_UNDEFINED, None])
    unleveled['logs'].append('plain text entry')
    on_stub(status_stub('st_unleveled', unleveled), '--error-level diag config status', 0)

    # Levels no server sends count as the highest, so they keep a failed reload failing at any --error-level.
    for name, level in (('range', 42), ('neg', -1), ('qrange', '42'), ('text', 'warn'), ('null', None)):
        bad = task('fail')
        bad['logs'] = [{'level': level, 'text': 'bad level'}]
        on_stub(status_stub(f'st_lvl_{name}', bad), '--error-level emergency config status', 2)

    # On a task that did not fail, a malformed level counts as at most a warning, like any other entry there. It prints
    # without a tag, since the server sent no level traffic_ctl knows.
    bad_sub = task('success')
    bad_sub['logs'] = [{'level': 'warn', 'text': 'level sent as text'}]
    ctl = status_stub('st_lvl_ok', task('success', [], [bad_sub]))
    tr = on_stub(ctl, '--error-level emergency config status', 0)
    assert has(tr, r'^[^\[\n]*level sent as text'), tr.output
    assert not has(tr, r'\[Emrg\]'), tr.output
    on_stub(ctl, '--error-level warn config status', 2)

    # traffic_server sends the level quoted, as every scalar.
    quoted = task('success')
    quoted['logs'] = [{'level': str(DL_WARNING), 'text': 'quoted warning'}]
    ctl = status_stub('st_quoted', quoted)
    on_stub(ctl, 'config status', 0)
    on_stub(ctl, '--error-level warn config status', 2)

    # A state traffic_ctl does not know is not graded, like a running one: the monitor keeps polling such a reload too.
    on_stub(status_stub('st_unknown', task('invalid', [DL_FATAL])), '--error-level diag config status', 0)
    ctl = status_stub('st_unknown_sub', task('success', [], [task('skipped', [DL_ERROR])]))
    on_stub(ctl, 'config status', 0)
    on_stub(ctl, '--error-level warn config status', 2)

    # A running reload is not graded, whatever it logged so far.
    ctl = status_stub('st_running', task('in_progress', [DL_FATAL], [task('fail', [DL_FATAL])]))
    on_stub(ctl, '--error-level diag config status', 0)

    # --count: the server lists the oldest reload first. Only the newest one is graded; the others are history.
    ctl = status_stub('st_hist_old', task('fail', [DL_ERROR], token='old'), task('success', [DL_NOTE], token='new'))
    tr = on_stub(ctl, 'config status -c 2', 0)
    assert has(tr, r'Reload \[fail\] — old'), tr.output
    assert has(tr, r'Reload \[success\] — new'), tr.output
    ctl = status_stub('st_hist_new', task('success', [DL_NOTE], token='old'), task('fail', [DL_ERROR], token='new'))
    tr = on_stub(ctl, 'config status -c 2', 2)
    assert has(tr, r'Reload \[success\] — old'), tr.output

    # A sub-task that failed is an error even under a parent that reports success.
    on_stub(status_stub('st_odd_tree', task('success', [], [task('success'), task('fail')])), 'config status', 2)
    ctl = status_stub('st_odd_to', task('success', [], [task('success'), task('timeout')]))
    on_stub(ctl, 'config status', 2)
    on_stub(ctl, '--error-level fatal config status', 75)

    # config reload --monitor polls until the reload ends, then grades it. Each node serves its replies in turn and
    # starts over after the last, so every run against it sees in_progress first.
    RELOAD_SCHEDULED = {'result': {'token': 'r1', 'message': ['Reload task scheduled']}}
    MONITOR = 'config reload -t r1 -m --initial-wait 0 --refresh-int 0.1'
    INLINE = "config reload -t r1 -m --initial-wait 0 --refresh-int 0.1 -d 'ip_allow: {}'"

    def monitor_stub(name: str, final: dict) -> str:
        """Start a stand-in RPC node that schedules a reload, reports it running, then @a final.

        :param name: Unique process name.
        :param final: get_reload_config_status reply after the running one.
        """
        return stub(
            name, {
                'admin_config_reload': RELOAD_SCHEDULED,
                'get_reload_config_status': [status_reply(task('in_progress')), final]
            })

    ctl = monitor_stub('mon_warn', status_reply(task('success', [], [task('success', [DL_WARNING])])))
    tr = on_stub(ctl, MONITOR, 0)
    assert not has(tr, 'Details :'), tr.output
    tr = on_stub(ctl, f'--error-level warn {MONITOR}', 2)
    assert has(tr, 'Details : traffic_ctl config status -t r1'), tr.output

    ctl = monitor_stub('mon_err', status_reply(task('success', [], [task('success', [DL_ERROR])])))
    tr = on_stub(ctl, MONITOR, 0)
    assert not has(tr, 'Details :'), tr.output

    ctl = monitor_stub('mon_fail', status_reply(task('fail', [], [task('fail', [DL_ERROR])])))
    on_stub(ctl, MONITOR, 2)
    tr = on_stub(ctl, f'--error-level fatal {MONITOR}', 0)
    # A failed reload still points at its details.
    assert has(tr, 'Details : traffic_ctl config status -t r1'), tr.output

    ctl = monitor_stub('mon_timeout', status_reply(task('timeout')))
    tr = on_stub(ctl, MONITOR, 2)
    assert has(tr, r'\d+/\d+ +timeout'), tr.output
    tr = on_stub(ctl, f'--error-level fatal {MONITOR}', 75)
    # A reload that never finished points at its details.
    assert has(tr, 'Details : traffic_ctl config status -t r1'), tr.output

    # Inline configuration that a monitored reload applied is live in memory only: the note says so, even when the grade
    # fails the command.
    ctl = monitor_stub('mon_inline', status_reply(task('success', [], [task('success', [DL_WARNING])])))
    tr = on_stub(ctl, f'--error-level warn {INLINE}', 2)
    assert has(tr, NOTE), tr.output
    ctl = monitor_stub('mon_inline_f', status_reply(task('fail', [], [task('fail', [DL_ERROR])])))
    tr = on_stub(ctl, INLINE, 2)
    # No note: nothing was applied.
    assert not has(tr, NOTE), tr.output
    tr = on_stub(ctl, f'--error-level fatal {INLINE}', 0)
    # No note: a relaxed failure applied nothing either.
    assert not has(tr, NOTE), tr.output

    # A reload already in progress refuses the inline request; the monitor follows that other reload, which does not carry it.
    IN_PROGRESS_REFUSAL = {
        'result':
            {
                'token': 'other',
                'errors': [{
                    'code': 6004,
                    'message': 'Reload in progress'
                }],
                'tasks': [task('in_progress', token='other')]
            }
    }
    ctl = stub(
        'mon_busy', {
            'admin_config_reload': IN_PROGRESS_REFUSAL,
            'get_reload_config_status':
                [
                    status_reply(task('in_progress', token='other')),
                    status_reply(task('success', [], [task('success', [DL_WARNING])], token='other'))
                ]
        })
    tr = on_stub(ctl, f'--error-level warn {INLINE}', 2)
    assert has(tr, r'Reload in progress \[other\]'), tr.output
    assert not has(tr, NOTE), tr.output
    # Without --monitor, a reload already in progress refuses the inline request too: no note there either.
    tr = on_stub(ctl, "config reload -t r1 -d 'ip_allow: {}'", 75)
    assert has(tr, r'Reload in progress \[other\]'), tr.output
    assert not has(tr, NOTE), tr.output

    # The note follows how the monitored reload ended, not its grade: a reload the monitor stopped watching may still apply
    # it, one that ended fail or timeout did not, and one whose state could not be read keeps the rule of the other commands.
    ctl = stub('mon_spin', {'admin_config_reload': RELOAD_SCHEDULED, 'get_reload_config_status': status_reply(task('in_progress'))})
    tr = on_stub(ctl, f'{INLINE} --timeout 300ms', 75)
    assert has(tr, 'Monitor timed out'), tr.output
    assert has(tr, NOTE), tr.output

    ctl = monitor_stub('mon_inline_to', status_reply(task('timeout')))
    tr = on_stub(ctl, INLINE, 2)
    assert not has(tr, NOTE), tr.output
    tr = on_stub(ctl, f'--error-level fatal {INLINE}', 75)
    assert not has(tr, NOTE), tr.output

    ctl = monitor_stub('mon_inline_odd', status_reply(task('success', [], [task('success'), task('timeout')])))
    tr = on_stub(ctl, f'--error-level fatal {INLINE}', 75)
    assert has(tr, 'Details : traffic_ctl config status -t r1'), tr.output
    # The note: the reload reported success.
    assert has(tr, NOTE), tr.output

    POLL_ERROR = {
        'error': {
            'code': 9,
            'message': 'Error during execution',
            'data': [{
                'code': 1,
                'message': 'boom',
                'severity': DL_ERROR
            }]
        }
    }
    ctl = stub(
        'mon_perr', {
            'admin_config_reload': RELOAD_SCHEDULED,
            'get_reload_config_status': [status_reply(task('in_progress')), POLL_ERROR]
        })
    tr = on_stub(ctl, INLINE, 2)
    # No note when the poll failed at the level.
    assert not has(tr, NOTE), tr.output
    tr = on_stub(ctl, f'--error-level fatal {INLINE}', 75)
    # Below the level the outcome is unknown: the note, as for the other commands.
    assert has(tr, NOTE), tr.output

    ctl = stub('mon_empty', {'admin_config_reload': RELOAD_SCHEDULED, 'get_reload_config_status': {'result': {'tasks': []}}})
    tr = on_stub(ctl, INLINE, 2)
    # No note when no task was reported.
    assert not has(tr, NOTE), tr.output

    # --count: only the newest reload shown decides, whatever it is.
    ctl = status_stub('st_c_run', task('fail', [DL_ERROR], token='old'), task('in_progress', token='new'))
    on_stub(ctl, 'config status -c 2', 0)
    ctl = status_stub('st_c_to', task('fail', [DL_FATAL], [task('fail', [DL_FATAL])], token='old'), task('timeout', token='new'))
    on_stub(ctl, '--error-level fatal config status -c 2', 75)
    ctl = status_stub(
        'st_c3', task('fail', token='a'), task('success', token='b'),
        task('success', [], [task('success', [DL_WARNING])], token='c'))
    on_stub(ctl, '--error-level warn config status -c 3', 2)
    on_stub(ctl, 'config status -c 3', 0)

    # A level outside the known range prints as it always did, clamped to the lowest or highest level, and still grades as
    # the highest severity.
    out_sub = task('success')
    out_sub['logs'] = [{'level': 42, 'text': 'level forty-two'}, {'level': -1, 'text': 'level minus one'}]
    ctl = status_stub('st_lvl_out', task('success', [], [out_sub]))
    tr = on_stub(ctl, 'config status', 0)
    assert has(tr, r'\[Emrg\] +level forty-two'), tr.output
    assert has(tr, r'\[Diag\] +level minus one'), tr.output
    tr = on_stub(ctl, 'config status --min-level note', 0)
    # The lowest level is filtered as before.
    assert not has(tr, 'level minus one'), tr.output
    on_stub(ctl, '--error-level warn config status', 2)


def test_reload_error_level_timeout(ats_factory: ATSFactory) -> None:
    """Grade a reload that traffic_server times out.

    The deferred test plugin finishes its reload after about 5 s; the progress checker marks the reload timeout after
    1 s.

    :param ats_factory: Proxy factory.
    """
    ats = ats_factory.create('ts')
    ats.records.update(
        {
            'proxy.config.admin.reload.timeout': '1s',
            'proxy.config.admin.reload.check_interval': '1s',
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'cfg_plugin_deferred_test',
        })
    ats.write_config_file('cfg_plugin_deferred_test.conf', 'mode: success\n')
    ats.copy_custom_plugin('{AtsBuildUraniumTestsDir}/jsonrpc/plugins/.libs/cfg_plugin_deferred_test.so')
    ats.plugin_config.add_line('cfg_plugin_deferred_test.so cfg_plugin_deferred_test.conf')
    ats.start()
    INLINE = "-m -w 0 -r 0.2 -d 'cfg_plugin_deferred_test: {mode: ok}'"

    tr = run(ats, f'traffic_ctl config reload -t to_def {INLINE}', 2)
    assert has(tr, r'\d+/\d+ +timeout'), tr.output
    # No note for a reload that ended timeout.
    assert not has(tr, NOTE), tr.output
    run(ats, 'traffic_ctl config status -t to_def', 2)

    # Let the deferred handler finish before the next reload.
    wait_for_file_lines(ats.traffic_out, 'Stage 1: heavy work done', 1, timeout=15)
    tr = run(ats, f'traffic_ctl --error-level fatal config reload -t to_fat {INLINE}', 75)
    assert has(tr, r'\d+/\d+ +timeout'), tr.output
    # A reload that never finished points at its details.
    assert has(tr, 'Details : traffic_ctl config status -t to_fat'), tr.output
    assert not has(tr, NOTE), tr.output
