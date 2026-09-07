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

import atexit
import json
import os
import shlex
import shutil
import sys
import tempfile

_gold_tmpdir = None


def _get_gold_tmpdir():
    """Return a temporary directory for generated gold files.

    The directory is created on first call and registered for cleanup at
    process exit so generated gold files never accumulate in /tmp.
    """
    global _gold_tmpdir
    if _gold_tmpdir is None:
        _gold_tmpdir = tempfile.mkdtemp(prefix='autest_gold_')
        atexit.register(shutil.rmtree, _gold_tmpdir, True)
    return _gold_tmpdir


def MakeGoldFileWithText(content, dir, test_number, add_new_line=True):
    """Write expected-output text to a temporary gold file and return its path.

    The gold file is placed in a process-unique temporary directory rather than
    the source tree so that generated files don't pollute the repository.

    Args:
        content: The expected output text.
        dir: Unused (kept for API compatibility).
        test_number: Numeric identifier used to name the gold file.
        add_new_line: If True, append a trailing newline to content.

    Returns:
        Absolute path to the generated gold file.
    """
    data_path = os.path.join(_get_gold_tmpdir(), "gold")
    os.makedirs(data_path, exist_ok=True)
    gold_filepath = os.path.join(data_path, f'test_{test_number}.gold')
    with open(gold_filepath, 'w') as gold_file:
        if add_new_line:
            content = f"{content}\n"
        gold_file.write(content)

    return gold_filepath


def _test_file_globals():
    """Return the globals of the calling test file.

    autest injects `Testers` and `All` into each test file's globals rather
    than exposing them for import, so a helper module has to reach up the
    stack to find them. The search walks outward until it reaches a frame
    that carries the injected names, rather than assuming the immediate
    caller is the test file. That way it works from inside this module and
    from any intermediate helper module.
    """
    frame = sys._getframe(1)
    while frame is not None and 'Testers' not in frame.f_globals:
        frame = frame.f_back
    if frame is None:
        raise RuntimeError('No autest test file frame found. These helpers only work when called from a test file.')
    return frame.f_globals


def _read_stdout(path):
    """Read a captured stream file, tolerating output that is not valid UTF-8."""
    with open(path, errors='replace') as stream:
        return stream.read()


def _check_is_valid_json(path):
    """Tester callback: the captured output must parse as JSON."""
    desc = "Check that the output parses as JSON"
    raw = _read_stdout(path)
    try:
        json.loads(raw)
    except ValueError as ex:
        return (False, desc, f"Output is not JSON: {ex}\nOutput was:\n{raw}")
    return (True, desc, "Output parses as JSON")


def _check_json_fields(path, expected):
    """Tester callback: every expected field must match its value in the parsed output."""
    desc = "Check that the JSON output contains the expected fields"
    raw = _read_stdout(path)
    try:
        doc = json.loads(raw)
    except ValueError as ex:
        return (False, desc, f"Output is not JSON: {ex}\nOutput was:\n{raw}")

    failed = [f"{key} = {doc.get(key)} (expected {value})" for key, value in expected.items() if str(doc.get(key)) != value]
    if failed:
        return (False, desc, "FAIL: " + "; ".join(failed) + f"\nOutput was:\n{raw}")
    return (True, desc, "All expected fields matched")


class Common():
    """
        Handy class to map common traffic_ctl test options.
    """

    def __init__(self, tr):
        self._tr = tr

    def _finish(self):
        """
            Sets the command to the test. Make sure this gets called after
            validation is set. Without this call the test will fail.
        """
        self._tr.Processes.Default.Command = self._cmd

    def exec(self):
        """
        If you need to just run the command with no validation, this is ok in the context of a test, but not to be
        used in isolation (as to run traffic_ctl commands)
        """
        self._finish()

    def validate_with_exit_code(self, exit_code: int):
        """
            Sets the exit code for the test.
        """
        self._tr.Processes.Default.ReturnCode = exit_code
        self._finish()
        return self

    def validate_with_text(self, text: str):
        """
        Validate command output matches expected text exactly.
        If text is empty, validates that output is completely empty (no newline).

        Example:
            traffic_ctl.config().get("proxy.config.product_name").validate_with_text("Apache Traffic Server")
            traffic_ctl.config().diff().validate_with_text("")  # expects empty output
        """
        self._tr.Processes.Default.Streams.stdout = MakeGoldFileWithText(text, self._dir, self._tn, text != "")
        self._finish()
        return self

    def validate_contains_all(self, *strings):
        """
        Validate command output contains all specified strings (order independent).
        Uses Testers.IncludesExpression for each string.

        Example:
            traffic_ctl.config().reset("proxy.config.diags").validate_contains_all(
                "Set proxy.config.diags.debug.tags",
                "Set proxy.config.diags.debug.enabled"
            )
        """
        caller_globals = _test_file_globals()
        _Testers = caller_globals['Testers']
        _All = caller_globals['All']
        testers = [_Testers.IncludesExpression(s, f"should contain: {s}") for s in strings]
        self._tr.Processes.Default.Streams.stdout = _All(*testers)
        self._finish()
        return self

    def validate_result_with_text(self, text: str):
        """
        Validate RPC result matches expected JSON exactly. Wraps text in JSON-RPC envelope.

        Example:
            traffic_ctl.rpc().invoke(handler="get_connection_tracker_info").validate_result_with_text(
                '{"outbound": {"count": "0", "list": []}}'
            )
        """
        full_text = f'{{\"jsonrpc\": \"2.0\", \"result\": {text}, \"id\": {"``"}}}'
        self._tr.Processes.Default.Streams.stdout = MakeGoldFileWithText(full_text, self._dir, self._tn)
        self._finish()
        return self

    def validate_json_contains(self, **field_checks):
        """
        Validate JSON output contains specific field:value pairs. Only checks specified fields.
        Every mismatch is reported as "field_name = actual_value (expected expected_value)",
        followed by the raw output.

        The check runs in the autest process against the captured stdout file. Piping
        traffic_ctl into a JSON parser instead would hide failures: the exit status of a shell
        pipeline is the parser's, so a non-zero traffic_ctl exit would never reach the
        ReturnCode check.

        Example:
            traffic_ctl.server().status().validate_json_contains(
                initialized_done='true', is_draining='false'
            )
        """
        _Testers = _test_file_globals()['Testers']
        self._tr.Processes.Default.Streams.stdout = _Testers.Lambda(
            lambda info, tester: _check_json_fields(tester.GetContent(info), field_checks))
        self._finish()
        return self

    def validate_is_valid_json(self):
        """
        Validate that stdout parses as JSON. Performs no field checks.

        Use this as a regression guard on any command documented to emit JSON.
        A gold file cannot do this job: yaml-cpp spells null as `~`, which a
        gold file matches happily but no JSON parser accepts.

        The check runs in the autest process against the captured stdout file, so
        traffic_ctl stays the only process in the test run and the exit status the
        harness compares against ReturnCode is still traffic_ctl's own. The raw
        output is reported on failure.

        Example:
            traffic_ctl.hostdb().status().validate_is_valid_json()
        """
        _Testers = _test_file_globals()['Testers']
        self._tr.Processes.Default.Streams.stdout = _Testers.Lambda(
            lambda info, tester: _check_is_valid_json(tester.GetContent(info)))
        self._finish()
        return self


class ConfigReload(Common):
    """
        Handy class to map traffic_ctl config reload options.

        Options (in command order):
            --token, -t          Configuration token
            --monitor, -m        Monitor reload progress until completion
            --show-details, -s   Show detailed information of the reload
            --include-logs, -l   Include logs (with --show-details)
            --refresh-int, -r    Refresh interval in seconds (with --monitor). Accepts fractional values
            --force, -F          Force reload even if one in progress
            --data, -d           Inline config data (@file1 @file2, @- for stdin, or yaml string)
            --initial-wait, -w   Initial wait before first poll (seconds). Accepts fractional values
    """

    def __init__(self, dir, tr, tn):
        super().__init__(tr)
        self._cmd = "traffic_ctl config reload"
        self._tr = tr
        self._dir = dir
        self._tn = tn

    def __finish(self):
        """
            Sets the command to the test. Make sure this gets called after
            validation is set. Without this call the test will fail.
        """
        self._tr.Processes.Default.Command = self._cmd

    # --- Options in command order ---

    def token(self, token: str):
        """Set a custom token for the reload (--token, -t)"""
        self._cmd = f'{self._cmd} --token {token} '
        return self

    def monitor(self):
        """Monitor reload progress until completion (--monitor, -m)"""
        self._cmd = f'{self._cmd} --monitor '
        return self

    def show_details(self):
        """Show detailed information of the reload (--show-details, -s)"""
        self._cmd = f'{self._cmd} --show-details '
        return self

    def include_logs(self):
        """Include logs in details (--include-logs, -l). Use with show_details()"""
        self._cmd = f'{self._cmd} --include-logs '
        return self

    def refresh_int(self, seconds: float):
        """Set refresh interval in seconds (--refresh-int, -r). Use with monitor(). Accepts fractional values (e.g. 0.5)"""
        self._cmd = f'{self._cmd} --refresh-int {seconds} '
        return self

    def force(self):
        """Force reload even if one in progress (--force, -F)"""
        self._cmd = f'{self._cmd} --force '
        return self

    def data(self, data_arg: str):
        """Set inline YAML data string (--data, -d)"""
        self._cmd = f'{self._cmd} --data \'{data_arg}\' '
        return self

    def data_file(self, filepath: str):
        """Set file-based inline data (--data @filepath, -d @filepath)"""
        self._cmd = f'{self._cmd} --data @{filepath} '
        return self

    def data_files(self, filepaths: list):
        """Set multiple file-based inline data (--data @file1 @file2 ...)"""
        files_str = ' '.join([f'@{fp}' for fp in filepaths])
        self._cmd = f'{self._cmd} --data {files_str} '
        return self

    def initial_wait(self, seconds: float):
        """Set initial wait before first poll (--initial-wait, -w). Use with monitor() or show_details()"""
        self._cmd = f'{self._cmd} --initial-wait {seconds} '
        return self

    # --- Validation ---

    def validate_with_text(self, text: str):
        self._tr.Processes.Default.Streams.stdout = MakeGoldFileWithText(text, self._dir, self._tn)
        self.__finish()


class ConfigStatus(Common):
    """
        Handy class to map traffic_ctl config status.
    """

    def __init__(self, dir, tr, tn):
        super().__init__(tr)
        self._cmd = "traffic_ctl config status"
        self._tr = tr
        self._dir = dir
        self._tn = tn

    def __finish(self):
        """
            Sets the command to the test. Make sure this gets called after
            validation is set. Without this call the test will fail.
        """
        self._tr.Processes.Default.Command = self._cmd

    def token(self, token: str):
        self._cmd = f'{self._cmd} --token {token} '
        return self

    def count(self, count: str):
        self._cmd = f'{self._cmd} --count {count}'
        return self

    def validate_with_text(self, text: str):
        self._tr.Processes.Default.Streams.stdout = MakeGoldFileWithText(text, self._dir, self._tn)
        self.__finish()


class Config(Common):
    """
        Handy class to map traffic_ctl config options.
    """

    def __init__(self, dir, tr, tn):
        super().__init__(tr)
        self._cmd = "traffic_ctl config "
        self._dir = dir
        self._tn = tn

    def diff(self):
        self._cmd = f'{self._cmd} diff'
        return self

    def get(self, value):
        self._cmd = f'{self._cmd} get {value}'
        return self

    def match(self, value):
        self._cmd = f'{self._cmd}  match {value}'
        return self

    def set(self, record, value):
        """
        Set a configuration record to a specific value.

        Args:
            record: The record name (e.g., "proxy.config.diags.debug.enabled")
            value: The value to set

        Example:
            traffic_ctl.config().set("proxy.config.diags.debug.enabled", "1")
        """
        self._cmd = f'{self._cmd} set {record} {value}'
        return self

    def describe(self, value):
        self._cmd = f'{self._cmd}  describe {value}'
        return self

    def reset(self, *paths):
        """
        Reset configuration values matching path pattern(s) to their defaults.

        Args:
            *paths: One or more path patterns (e.g., "records", "proxy.config.http",
                   "proxy.config.diags.debug.enabled")

        Example:
            traffic_ctl.config().reset("records")
            traffic_ctl.config().reset("proxy.config.http")
            traffic_ctl.config().reset("proxy.config.diags.debug.enabled")
        """
        if not paths:
            self._cmd = f'{self._cmd} reset records'
        else:
            paths_str = ' '.join(paths)
            self._cmd = f'{self._cmd} reset {paths_str}'
        return self

    def reload(self):
        return ConfigReload(self._dir, self._tr, self._tn)

    def status(self):
        return ConfigStatus(self._dir, self._tr, self._tn)

    def as_records(self):
        self._cmd = f'{self._cmd} --records'
        return self

    def with_default(self):
        self._cmd = f'{self._cmd}  --default'
        return self

    def validate_with_goldfile(self, file: str):
        self._tr.Processes.Default.Streams.stdout = os.path.join("gold", file)
        self._finish()


class Debug(Common):
    """
        Handy class to map traffic_ctl server debug options.
    """

    def __init__(self, dir, tr, tn):
        super().__init__(tr)
        self._cmd = "traffic_ctl server debug "
        self._dir = dir
        self._tn = tn

    def enable(self, tags=None, append=False, client_ip=None):
        """
        Enable debug logging at runtime.

        Args:
            tags: Debug tags to set (e.g., "http|dns")
            append: If True, append tags to existing tags instead of replacing
            client_ip: Client IP filter for debug output

        Example:
            traffic_ctl.server().debug().enable(tags="http").exec()
            traffic_ctl.server().debug().enable(tags="dns", append=True).exec()
        """
        self._cmd = f'{self._cmd} enable'
        if tags:
            self._cmd = f'{self._cmd} --tags {tags}'
        if append:
            self._cmd = f'{self._cmd} --append'
        if client_ip:
            self._cmd = f'{self._cmd} --client_ip {client_ip}'
        return self

    def disable(self):
        """
        Disable debug logging at runtime.

        Example:
            traffic_ctl.server().debug().disable().exec()
        """
        self._cmd = f'{self._cmd} disable'
        return self


class Server(Common):
    """
        Handy class to map traffic_ctl server options.
    """

    def __init__(self, dir, tr, tn):
        super().__init__(tr)
        self._cmd = "traffic_ctl server "
        self._dir = dir
        self._tn = tn

    def status(self):
        self._cmd = f'{self._cmd}  status '
        return self

    def drain(self, undo=False):
        self._cmd = f'{self._cmd}  drain '
        if undo:
            self._cmd = f'{self._cmd} --undo'
        return self

    def debug(self):
        """
        Returns a Debug object for debug enable/disable commands.

        Example:
            traffic_ctl.server().debug().enable(tags="http").exec()
            traffic_ctl.server().debug().disable().exec()
        """
        return Debug(self._dir, self._tr, self._tn)

    def as_json(self):
        self._cmd = f'{self._cmd} -f json'
        return self


class RPC(Common):
    """
        Handy class to map traffic_ctl server options.
    """

    def __init__(self, dir, tr, tn):
        super().__init__(tr)
        self._cmd = "traffic_ctl rpc "
        self._dir = dir
        self._tn = tn

    def invoke(self, handler: str, params={}):
        if not params:
            self._cmd = f'{self._cmd}  invoke {handler} -f json'
        else:
            self._cmd = f'{self._cmd}  invoke {handler} -p {str(params)} -f json'

        return self


class HostDB(Common):
    """
        Handy class to map traffic_ctl hostdb options.
    """

    def __init__(self, dir, tr, tn):
        super().__init__(tr)
        self._cmd = "traffic_ctl hostdb "
        self._dir = dir
        self._tn = tn

    def status(self, hostname: str = ""):
        """Get HostDB info (traffic_ctl hostdb status [HOSTNAME])

        The hostname is shell quoted. It is omitted entirely when empty, since
        passing an empty argument is not the same as passing none.
        """
        arg = f' {shlex.quote(hostname)}' if hostname else ''
        self._cmd = f'{self._cmd} status{arg} '
        return self

    def as_json(self):
        self._cmd = f'{self._cmd} -f json'
        return self


class Plugin(Common):
    """
        Handy class to map traffic_ctl plugin options.
    """

    def __init__(self, dir, tr, tn):
        super().__init__(tr)
        self._cmd = "traffic_ctl plugin "
        self._dir = dir
        self._tn = tn

    def list(self):
        """Show globally loaded plugins and their status (traffic_ctl plugin list)"""
        self._cmd = f'{self._cmd} list '
        return self

    def as_json(self):
        self._cmd = f'{self._cmd} -f json'
        return self


'''

Handy wrapper around traffic_ctl, ATS and the autest output validation mechanism.
The Idea is to use this as a way to validate traffic_ctl output and not to execute traffic_ctl command(though it can and is
recommended in the context of  test)

Example for a single test.

# create the traffic_ctl wrapper.
traffic_ctl = Make_traffic_ctl(Test, records_yaml)

## if the output is simple, then you can just
traffic_ctl.config().get("proxy.config.diags.debug.enabled").validate_with_text("proxy.config.diags.debug.enabled: 1")

# if the poutput is a bit complex, then you can just set your own gold file.
traffic_ctl.config().get("proxy.config.diags.debug.tags").as_records().validate_with_goldfile("your_gold_file.gold")


'''


class TrafficCtl(Config, Server):
    """
        Single TS instance with multiple tests.
        Every time a config() is called, a new test is created.
    """

    def __init__(self, test, records_yaml=None, retcode=0):
        self._testNumber = 0
        self._current_test_number = self._testNumber
        self._retcode = retcode
        self._Test = test
        self._ts = self._Test.MakeATSProcess(f"ts_{self._testNumber}")
        if records_yaml != None:
            self._ts.Disk.records_config.update(records_yaml)
        self._tests = []

    def __get_index(self):
        return self._current_test_number

    def add_test(self):

        tr = self._Test.AddTestRun(f"test {self._testNumber}")
        if self._testNumber == 0:
            tr.Processes.Default.StartBefore(self._ts)
        self._testNumber = self._testNumber + 1

        tr.Processes.Default.Env = self._ts.Env
        tr.DelayStart = 3
        tr.Processes.Default.ReturnCode = self._retcode
        tr.StillRunningAfter = self._ts

        self._tests.insert(self.__get_index(), tr)
        return self

    def config(self):
        self.add_test()
        return Config(self._Test.TestDirectory, self._tests[self.__get_index()], self._testNumber)

    def server(self):
        self.add_test()
        return Server(self._Test.TestDirectory, self._tests[self.__get_index()], self._testNumber)

    def rpc(self):
        self.add_test()
        return RPC(self._Test.TestDirectory, self._tests[self.__get_index()], self._testNumber)

    def hostdb(self):
        self.add_test()
        return HostDB(self._Test.TestDirectory, self._tests[self.__get_index()], self._testNumber)

    def plugin(self):
        self.add_test()
        return Plugin(self._Test.TestDirectory, self._tests[self.__get_index()], self._testNumber)


def Make_traffic_ctl(test, records_yaml=None, retcode=0):
    tctl = TrafficCtl(test, records_yaml, retcode)
    return tctl
