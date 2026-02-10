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
import os
import shutil
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
        import sys
        # Testers and All are injected by autest into the test file's globals
        caller_globals = sys._getframe(1).f_globals
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
        Prints detailed error on failure: "FAIL: field_name = actual_value (expected expected_value)"
        stream.all.txt will contain the actual output with the failed fields.

        Example:
            traffic_ctl.server().status().validate_json_contains(
                initialized_done='true', is_draining='false'
            )
        """
        import json
        checks_str = ', '.join(f"'{k}': '{v}'" for k, v in field_checks.items())
        self._cmd = (
            f'{self._cmd} | python3 -c "'
            f"import sys, json; "
            f"d = json.load(sys.stdin); "
            f"c = {{{checks_str}}}; "
            f"failed = [(k, v, str(d.get(k))) for k, v in c.items() if str(d.get(k)) != v]; "
            f"[print(f'FAIL: {{k}} = {{actual}} (expected {{expected}})', file=sys.stderr) "
            f"for k, expected, actual in failed]; "
            f"exit(0 if not failed else 1)"
            f'"')
        self._finish()
        return self


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

    def __init__(self, test, records_yaml=None):
        self._testNumber = 0
        self._current_test_number = self._testNumber

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
        tr.Processes.Default.ReturnCode = 0
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


def Make_traffic_ctl(test, records_yaml=None):
    tctl = TrafficCtl(test, records_yaml)
    return tctl
