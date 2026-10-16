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
traffic_ctl --error-level applied to config reload results: config reload
--monitor and config status grade a finished reload by the most severe log
entry of the task and its sub-tasks. The state of each task bounds its
entries: fail and timeout count as at least an error, and the entries of a
task that did not fail count as at most a warning, so at the default level the
exit status follows the state. A timeout below the level exits 75. Entries
without a severity are not graded, and a reload that is still running, or in a
state traffic_ctl does not know, is not graded. config status grades the
newest reload it shows.

The first half reloads a real traffic_server; the second half talks to a
stand-in JSONRPC node, for task trees no handler produces on demand.
'''

Test.ContinueOnFail = True

ts = Test.MakeATSProcess('ts')
ts.Disk.cache_config.AddLine('dest_domain=example.com ttl-in-cache=30d')
# The cache.config reloads below reject a line and fail to read the file, which traffic_server also reports in diags.log.
ts.Disk.diags_log.Content = Testers.ContainsExpression('not a valid IP address range', 'the bad cache.config line was reported')
ts.Disk.diags_log.Content += Testers.ContainsExpression('cache.config failed to load', 'the unreadable cache.config was reported')
config_dir = ts.Variables.CONFIGDIR
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

MONITOR_TS = '-m --initial-wait 0.5 --refresh-int 0.5 --timeout 30s'
cache_config = os.path.join(config_dir, 'cache.config')

# A cache.config line with a malformed IP is discarded: the cache.config task logs an error and still ends success.
tr = on_ts(
    'a reload that succeeds but logs an error passes at the default level',
    f"echo 'dest_ip=999.1.1.1 action=never-cache' >> {cache_config} && sleep 2 && "
    f"traffic_ctl config reload -t bad_line {MONITOR_TS}", 0)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'\d+/\d+ +success', 'the reload ended success')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression('Details :', 'no details hint')

tr = on_ts('config status shows the error entry', 'traffic_ctl config status -t bad_line', 0)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    r'\[Err\] +\[CacheControl\] not a valid IP address range', 'the error entry is shown')
on_ts('the error entry of a success counts as a warning', 'traffic_ctl --error-level warn config status -t bad_line', 2)
on_ts('json output is graded too', 'traffic_ctl -f json --error-level warn config status -t bad_line', 2)

# Put the line right: the next reload of cache.config is clean, apart from its notes.
tr = on_ts(
    'a clean reload passes at the default level', f"echo 'dest_domain=example.com ttl-in-cache=30d' > {cache_config} && sleep 2 && "
    f"traffic_ctl config reload -t clean {MONITOR_TS}", 0)
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression('Details :', 'no details hint for a clean reload')
on_ts('the clean reload passes at --error-level warn', 'traffic_ctl --error-level warn config status -t clean', 0)
on_ts('the clean reload fails at --error-level note: it logs notes', 'traffic_ctl --error-level note config status -t clean', 2)
on_ts(
    'entries hidden by --min-level are still graded', 'traffic_ctl --error-level note config status -t clean --min-level error', 2)

# A directory in place of cache.config cannot be read: the cache.config task fails.
tr = on_ts(
    'a failed reload fails at the default level', f"rm {cache_config} && mkdir {cache_config} && sleep 2 && "
    f"traffic_ctl config reload -t unreadable {MONITOR_TS}", 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'\d+/\d+ +fail', 'the reload ended fail')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'Details : traffic_ctl config status -t unreadable', 'the details hint is printed')
on_ts('config status grades the failed reload', 'traffic_ctl config status -t unreadable', 2)
on_ts(
    'config status, --error-level fatal relaxes the failed reload', 'traffic_ctl --error-level fatal config status -t unreadable',
    0)
on_ts(
    'put cache.config back; the default level, given explicitly, still fails the reload',
    f"rmdir {cache_config} && echo 'dest_domain=example.com ttl-in-cache=30d' > {cache_config} && "
    "traffic_ctl --error-level error config status -t unreadable", 2)

# ---------------------------------------------------------------------------------------------------------------------
# Against a stand-in JSONRPC node
# ---------------------------------------------------------------------------------------------------------------------


def stub(name: str, replies: dict):
    # name is a directory under the test's run directory that holds the stub's UNIX socket. Keep it short: macOS caps
    # socket paths at 104 bytes, and the sandbox path is already long.
    directory = os.path.join(Test.RunDirectory, name)
    process = Test.Processes.Process(
        name,
        f'{sys.executable} {os.path.join(Test.TestDirectory, "jsonrpc_error_stub.py")} {directory} {shlex.quote(json.dumps(replies))}'
    )
    process.Ready = When.FileExists(os.path.join(directory, 'ready'))
    return process, f'traffic_ctl --run-root={os.path.join(directory, "runroot.yaml")}'


def task(status: str, levels: list = (), sub_tasks: list = (), token: str = 'r1') -> dict:
    '''A reload task as get_reload_config_status reports it. A level of None is an entry sent without one.'''
    logs = [{'text': f'entry {i}'} if level is None else {'level': level, 'text': f'entry {i}'} for i, level in enumerate(levels)]
    return {'config_token': token, 'status': status, 'description': 'stub task', 'logs': logs, 'sub_tasks': list(sub_tasks)}


def status_reply(*tasks: dict) -> dict:
    return {'result': {'tasks': list(tasks)}}


DL_NOTE, DL_WARNING, DL_ERROR, DL_FATAL, DL_UNDEFINED = 3, 4, 5, 6, 9


def on_stub(description: str, process, ctl: str, args: str, return_code: int):
    tr = run(description, f'{ctl} {args}', return_code, process)
    # The exit status comes from the grade, not from a reply traffic_ctl could not decode.
    tr.Processes.Default.Streams.stderr = Testers.ExcludesExpression('Error found', 'the reply was decoded')
    if 'config status' in args:
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'Reload \[', 'the report is printed')
    return tr


def status_stub(name: str, *tasks: dict):
    return stub(name, {'get_reload_config_status': status_reply(*tasks)})


# success with a warning two levels down: sub-task entries count, at any depth.
warn_node, warn_ctl = status_stub(
    'st_warn', task('success', [DL_NOTE], [task('success', [DL_NOTE], [task('success', [DL_WARNING])])]))
on_stub('success with a nested warning', warn_node, warn_ctl, 'config status', 0)
on_stub('success with a nested warning, --error-level warn', warn_node, warn_ctl, '--error-level warn config status', 2)
on_stub(
    'success with a nested warning, hidden by --min-level, still graded', warn_node, warn_ctl,
    '--error-level warn config status --min-level error', 2)

# success with a fatal entry: the entries of a task that succeeded count as at most a warning.
capped_node, capped_ctl = status_stub('st_capped', task('success', [], [task('success', [DL_FATAL])]))
on_stub('success with a fatal entry', capped_node, capped_ctl, 'config status', 0)
on_stub('success with a fatal entry, --error-level warn', capped_node, capped_ctl, '--error-level warn config status', 2)

# fail without any entry: the state alone is an error, and fatal relaxes it.
bare_fail_node, bare_fail_ctl = status_stub('st_bare_fail', task('fail'))
tr = on_stub('fail without entries', bare_fail_node, bare_fail_ctl, 'config status', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'\[fail\]', 'the failed reload is shown')
on_stub('fail without entries, --error-level fatal', bare_fail_node, bare_fail_ctl, '--error-level fatal config status', 0)

# fail with a fatal entry in a sub-task that failed: fatal does not relax it.
fatal_node, fatal_ctl = status_stub('st_fatal', task('fail', [DL_ERROR], [task('fail', [DL_FATAL])]))
on_stub('fail with a fatal entry, --error-level fatal', fatal_node, fatal_ctl, '--error-level fatal config status', 2)
on_stub('fail with a fatal entry, --error-level alert', fatal_node, fatal_ctl, '--error-level alert config status', 0)

# timeout whose only entry has no severity, like the reason the progress checker logs.
timeout_node, timeout_ctl = status_stub('st_timeout', task('timeout', [DL_UNDEFINED]))
on_stub('timeout', timeout_node, timeout_ctl, 'config status', 2)
on_stub('timeout, --error-level fatal: not done', timeout_node, timeout_ctl, '--error-level fatal config status', 75)

# Entries without a severity, sent as level 9, without a level, or as plain strings (an older server), are not graded.
unleveled = task('success', [DL_UNDEFINED, None])
unleveled['logs'].append('plain text entry')
unleveled_node, unleveled_ctl = status_stub('st_unleveled', unleveled)
on_stub('entries without a severity, --error-level diag', unleveled_node, unleveled_ctl, '--error-level diag config status', 0)

# Levels no server sends count as the highest, so they keep a failed reload failing at any --error-level.
for name, level in (('range', 42), ('neg', -1), ('qrange', '42'), ('text', 'warn'), ('null', None)):
    bad = task('fail')
    bad['logs'] = [{'level': level, 'text': 'bad level'}]
    node, ctl = status_stub(f'st_lvl_{name}', bad)
    on_stub(
        f'a failed reload with a level that is {name}, --error-level emergency', node, ctl, '--error-level emergency config status',
        2)

# On a task that did not fail, a malformed level counts as at most a warning, like any other entry there. It prints without a
# tag, since the server sent no level traffic_ctl knows.
bad_sub = task('success')
bad_sub['logs'] = [{'level': 'warn', 'text': 'level sent as text'}]
bad_ok_node, bad_ok_ctl = status_stub('st_lvl_ok', task('success', [], [bad_sub]))
tr = on_stub(
    'a success with a malformed level, --error-level emergency', bad_ok_node, bad_ok_ctl, '--error-level emergency config status',
    0)
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    r'(?m)^[^\[\n]*level sent as text', 'the entry is printed without a tag')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(r'\[Emrg\]', 'no severity is made up for it')
on_stub('a success with a malformed level, --error-level warn', bad_ok_node, bad_ok_ctl, '--error-level warn config status', 2)

# traffic_server sends the level quoted, as every scalar.
quoted = task('success')
quoted['logs'] = [{'level': str(DL_WARNING), 'text': 'quoted warning'}]
quoted_node, quoted_ctl = status_stub('st_quoted', quoted)
on_stub('a quoted warning', quoted_node, quoted_ctl, 'config status', 0)
on_stub('a quoted warning, --error-level warn', quoted_node, quoted_ctl, '--error-level warn config status', 2)

# A state traffic_ctl does not know is not graded, like a running one: the monitor keeps polling such a reload too.
unknown_node, unknown_ctl = status_stub('st_unknown', task('invalid', [DL_FATAL]))
on_stub('a state traffic_ctl does not know is not graded', unknown_node, unknown_ctl, '--error-level diag config status', 0)
unknown_sub_node, unknown_sub_ctl = status_stub('st_unknown_sub', task('success', [], [task('skipped', [DL_ERROR])]))
on_stub('a sub-task in a state traffic_ctl does not know', unknown_sub_node, unknown_sub_ctl, 'config status', 0)
on_stub(
    'a sub-task in a state traffic_ctl does not know, --error-level warn', unknown_sub_node, unknown_sub_ctl,
    '--error-level warn config status', 2)

# A running reload is not graded, whatever it logged so far.
running_node, running_ctl = status_stub('st_running', task('in_progress', [DL_FATAL], [task('fail', [DL_FATAL])]))
on_stub('a running reload is not graded', running_node, running_ctl, '--error-level diag config status', 0)

# --count: the server lists the oldest reload first. Only the newest one is graded; the others are history.
history_node, history_ctl = status_stub(
    'st_hist_old', task('fail', [DL_ERROR], token='old'), task('success', [DL_NOTE], token='new'))
tr = on_stub('with --count, an older failed reload is history', history_node, history_ctl, 'config status -c 2', 0)
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(r'Reload \[fail\] — old', 'the older reload is shown')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(r'Reload \[success\] — new', 'the newest reload is shown')
latest_node, latest_ctl = status_stub('st_hist_new', task('success', [DL_NOTE], token='old'), task('fail', [DL_ERROR], token='new'))
tr = on_stub('with --count, a failed newest reload fails the command', latest_node, latest_ctl, 'config status -c 2', 2)
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(r'Reload \[success\] — old', 'the older reload is shown')

# A sub-task that failed is an error even under a parent that reports success.
odd_node, odd_ctl = status_stub('st_odd_tree', task('success', [], [task('success'), task('fail')]))
on_stub('a failed sub-task under a successful parent', odd_node, odd_ctl, 'config status', 2)
odd_to_node, odd_to_ctl = status_stub('st_odd_to', task('success', [], [task('success'), task('timeout')]))
on_stub('a sub-task that timed out under a successful parent', odd_to_node, odd_to_ctl, 'config status', 2)
on_stub(
    'a sub-task that timed out under a successful parent, --error-level fatal: not done', odd_to_node, odd_to_ctl,
    '--error-level fatal config status', 75)

# config reload --monitor polls until the reload ends, then grades it.
RELOAD_SCHEDULED = {'result': {'token': 'r1', 'message': ['Reload task scheduled']}}
MONITOR = 'config reload -t r1 -m --initial-wait 0 --refresh-int 0.1'


def monitor_stub(name: str, final: dict):
    return stub(
        name, {
            'admin_config_reload': RELOAD_SCHEDULED,
            'get_reload_config_status': [status_reply(task('in_progress')), final]
        })


mon_warn_node, mon_warn_ctl = monitor_stub('mon_warn', status_reply(task('success', [], [task('success', [DL_WARNING])])))
tr = on_stub('monitor, success with a warning', mon_warn_node, mon_warn_ctl, MONITOR, 0)
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression('Details :', 'no details hint')
tr = on_stub('monitor, success with a warning, --error-level warn', mon_warn_node, mon_warn_ctl, f'--error-level warn {MONITOR}', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('Details : traffic_ctl config status -t r1', 'the details hint')

mon_err_node, mon_err_ctl = monitor_stub('mon_err', status_reply(task('success', [], [task('success', [DL_ERROR])])))
tr = on_stub('monitor, success with an error entry', mon_err_node, mon_err_ctl, MONITOR, 0)
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression('Details :', 'no details hint')

mon_fail_node, mon_fail_ctl = monitor_stub('mon_fail', status_reply(task('fail', [], [task('fail', [DL_ERROR])])))
on_stub('monitor, fail', mon_fail_node, mon_fail_ctl, MONITOR, 2)
tr = on_stub('monitor, fail, --error-level fatal', mon_fail_node, mon_fail_ctl, f'--error-level fatal {MONITOR}', 0)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    'Details : traffic_ctl config status -t r1', 'a failed reload still points at its details')

mon_timeout_node, mon_timeout_ctl = monitor_stub('mon_timeout', status_reply(task('timeout')))
tr = on_stub('monitor, timeout', mon_timeout_node, mon_timeout_ctl, MONITOR, 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'\d+/\d+ +timeout', 'the reload ended timeout')
tr = on_stub(
    'monitor, timeout, --error-level fatal: not done', mon_timeout_node, mon_timeout_ctl, f'--error-level fatal {MONITOR}', 75)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    'Details : traffic_ctl config status -t r1', 'a reload that never finished points at its details')

# Inline configuration that a monitored reload applied is live in memory only: the note says so, even when the grade fails
# the command.
INLINE = "config reload -t r1 -m --initial-wait 0 --refresh-int 0.1 -d 'ip_allow: {}'"
inline_node, inline_ctl = monitor_stub('mon_inline', status_reply(task('success', [], [task('success', [DL_WARNING])])))
tr = on_stub('inline reload with a warning, --error-level warn', inline_node, inline_ctl, f'--error-level warn {INLINE}', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('Inline configuration is NOT persisted', 'the note is printed')
inline_fail_node, inline_fail_ctl = monitor_stub('mon_inline_f', status_reply(task('fail', [], [task('fail', [DL_ERROR])])))
tr = on_stub('inline reload that failed', inline_fail_node, inline_fail_ctl, INLINE, 2)
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression(
    'Inline configuration is NOT persisted', 'no note: nothing was applied')
tr = on_stub(
    'inline reload that failed, --error-level fatal', inline_fail_node, inline_fail_ctl, f'--error-level fatal {INLINE}', 0)
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression(
    'Inline configuration is NOT persisted', 'no note: a relaxed failure applied nothing either')

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
busy_node, busy_ctl = stub(
    'mon_busy', {
        'admin_config_reload': IN_PROGRESS_REFUSAL,
        'get_reload_config_status':
            [
                status_reply(task('in_progress', token='other')),
                status_reply(task('success', [], [task('success', [DL_WARNING])], token='other'))
            ]
    })
tr = on_stub('inline reload refused by one in progress, --error-level warn', busy_node, busy_ctl, f'--error-level warn {INLINE}', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    r'Reload in progress \[other\]', 'the monitor follows the other reload')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    'Inline configuration is NOT persisted', 'no note: the inline config was refused')

# Without --monitor, a reload already in progress refuses the inline request too: no note there either.
tr = on_stub(
    'inline reload refused by one in progress, without --monitor', busy_node, busy_ctl, "config reload -t r1 -d 'ip_allow: {}'", 75)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'Reload in progress \[other\]', 'the refusal is reported')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    'Inline configuration is NOT persisted', 'no note: the inline config was refused')

# The note follows how the monitored reload ended, not its grade: a reload the monitor stopped watching may still apply it, one
# that ended fail or timeout did not, and one whose state could not be read keeps the rule of the other commands.
NOTE = 'Inline configuration is NOT persisted'
spin_node, spin_ctl = stub(
    'mon_spin', {
        'admin_config_reload': RELOAD_SCHEDULED,
        'get_reload_config_status': status_reply(task('in_progress'))
    })
tr = on_stub('inline, the monitor --timeout expires', spin_node, spin_ctl, f'{INLINE} --timeout 300ms', 75)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('Monitor timed out', 'the monitor timed out')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(NOTE, 'the note: the reload may still apply it')

srv_to_node, srv_to_ctl = monitor_stub('mon_inline_to', status_reply(task('timeout')))
tr = on_stub('inline, the server times the reload out', srv_to_node, srv_to_ctl, INLINE, 2)
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression(NOTE, 'no note for a reload that ended timeout')
tr = on_stub(
    'inline, the server times the reload out, --error-level fatal', srv_to_node, srv_to_ctl, f'--error-level fatal {INLINE}', 75)
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression(NOTE, 'no note at any level')

odd_inline_node, odd_inline_ctl = monitor_stub(
    'mon_inline_odd', status_reply(task('success', [], [task('success'), task('timeout')])))
tr = on_stub(
    'inline, success with a sub-task that timed out, --error-level fatal', odd_inline_node, odd_inline_ctl,
    f'--error-level fatal {INLINE}', 75)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('Details : traffic_ctl config status -t r1', 'the details hint')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(NOTE, 'the note: the reload reported success')


def poll_error(severity: int) -> dict:
    return {
        'error': {
            'code': 9,
            'message': 'Error during execution',
            'data': [{
                'code': 1,
                'message': 'boom',
                'severity': severity
            }]
        }
    }


perr_node, perr_ctl = stub(
    'mon_perr', {
        'admin_config_reload': RELOAD_SCHEDULED,
        'get_reload_config_status': [status_reply(task('in_progress')), poll_error(DL_ERROR)]
    })
tr = on_stub('inline, a status poll fails at the level', perr_node, perr_ctl, INLINE, 2)
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression(NOTE, 'no note when the poll failed')
tr = on_stub('inline, a status poll fails below the level', perr_node, perr_ctl, f'--error-level fatal {INLINE}', 75)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    NOTE, 'the note: the outcome is unknown, as for the other commands')

empty_node, empty_ctl = stub(
    'mon_empty', {
        'admin_config_reload': RELOAD_SCHEDULED,
        'get_reload_config_status': {
            'result': {
                'tasks': []
            }
        }
    })
tr = on_stub('inline, a status reply without tasks', empty_node, empty_ctl, INLINE, 2)
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression(NOTE, 'no note when no task was reported')

# --count: only the newest reload shown decides, whatever it is.
c_run_node, c_run_ctl = status_stub('st_c_run', task('fail', [DL_ERROR], token='old'), task('in_progress', token='new'))
on_stub('count 2, the newest still running, an older one failed', c_run_node, c_run_ctl, 'config status -c 2', 0)
c_to_node, c_to_ctl = status_stub(
    'st_c_to', task('fail', [DL_FATAL], [task('fail', [DL_FATAL])], token='old'), task('timeout', token='new'))
on_stub(
    'count 2, the newest timed out, an older one failed with fatal, --error-level fatal', c_to_node, c_to_ctl,
    '--error-level fatal config status -c 2', 75)
c3_node, c3_ctl = status_stub(
    'st_c3', task('fail', token='a'), task('success', token='b'), task('success', [], [task('success', [DL_WARNING])], token='c'))
on_stub('count 3, the newest with a warning, --error-level warn', c3_node, c3_ctl, '--error-level warn config status -c 3', 2)
on_stub('count 3, the newest with a warning', c3_node, c3_ctl, 'config status -c 3', 0)

# A level outside the known range prints as it always did, clamped to the lowest or highest level, and still grades as the
# highest severity.
out_sub = task('success')
out_sub['logs'] = [{'level': 42, 'text': 'level forty-two'}, {'level': -1, 'text': 'level minus one'}]
out_node, out_ctl = status_stub('st_lvl_out', task('success', [], [out_sub]))
tr = on_stub('levels out of range print clamped', out_node, out_ctl, 'config status', 0)
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    r'\[Emrg\] +level forty-two', 'above the range prints as the highest')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    r'\[Diag\] +level minus one', 'below the range prints as the lowest')
tr = on_stub('levels out of range, --min-level note', out_node, out_ctl, 'config status --min-level note', 0)
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression('level minus one', 'the lowest level is filtered as before')
on_stub('levels out of range, --error-level warn', out_node, out_ctl, '--error-level warn config status', 2)

# ---------------------------------------------------------------------------------------------------------------------
# A reload traffic_server times out
# ---------------------------------------------------------------------------------------------------------------------

# The deferred test plugin finishes its reload after about 5 s; the progress checker marks the reload timeout after 1 s.
ts_to = Test.MakeATSProcess('ts_to', dump_runroot=True)
ts_to.Disk.records_config.update({
    'proxy.config.admin.reload.timeout': '1s',
    'proxy.config.admin.reload.check_interval': '1s',
})
ts_to.Disk.MakeConfigFile('cfg_plugin_deferred_test.conf').AddLines(['mode: success'])
Test.PrepareTestPlugin(
    os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'jsonrpc', 'plugins', '.libs', 'cfg_plugin_deferred_test.so'), ts_to,
    'cfg_plugin_deferred_test.conf')
TS_TO_INLINE = "-m -w 0 -r 0.2 -d 'cfg_plugin_deferred_test: {mode: ok}'"


def on_ts_to(description: str, command: str, return_code: int, delay: int = 0):
    tr = Test.AddTestRun(description)
    tr.Processes.Default.Command = command
    tr.Processes.Default.Env = ts_to.Env
    tr.Processes.Default.ReturnCode = return_code
    if delay:
        tr.DelayStart = delay
    if ts_to.Name not in started:
        tr.Processes.Default.StartBefore(ts_to)
        started.add(ts_to.Name)
    tr.StillRunningAfter = ts_to
    return tr


tr = on_ts_to(
    'a reload traffic_server times out fails at the default level', f'traffic_ctl config reload -t to_def {TS_TO_INLINE}', 2)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'\d+/\d+ +timeout', 'the reload ended timeout')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(NOTE, 'no note for a reload that ended timeout')
on_ts_to('config status grades the reload that timed out', 'traffic_ctl config status -t to_def', 2)
# Let the deferred handler finish before the next reload.
tr = on_ts_to(
    'a reload traffic_server times out, --error-level fatal: not done',
    f'traffic_ctl --error-level fatal config reload -t to_fat {TS_TO_INLINE}',
    75,
    delay=8)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(r'\d+/\d+ +timeout', 'the reload ended timeout')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'Details : traffic_ctl config status -t to_fat', 'a reload that never finished points at its details')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(NOTE, 'no note at any level')
