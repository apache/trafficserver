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

# This function can(eventually) be used to have a single yaml file and read nodes from it.
# The idea would be to avoid having multiple gold files with yaml content.
# The only issue would be the comments, this is because how the yaml lib reads yaml,
# comments  aren't rendered in the same way as traffic_ctl throws it, it should only
# be used if no comments need to be compared.
#
# def GoldFilePathFor(node:str, main_file="gold/test_gold_file.yaml"):
#     if node == "":
#         raise Exception("node should not be empty")

#     yaml = ruamel.yaml.YAML()
#     yaml.indent(sequence=4, offset=2)
#     with open(os.path.join(Test.TestDirectory, main_file), 'r') as f:
#         content = yaml.load(f)

#     node_data = content[node]
#     data_dirname = 'generated_gold_files'
#     data_path = os.path.join(Test.TestDirectory, data_dirname)
#     os.makedirs(data_path, exist_ok=True)
#     gold_filepath = os.path.join(data_path, f'test_{TestNumber}.gold')
#     with open(os.path.join(data_path, f'test_{TestNumber}.gold'), 'w') as gold_file:
#         yaml.dump(node_data, gold_file)

#     return gold_filepath


def MakeGoldFileWithText(content, dir, test_number, add_new_line=True):
    data_path = os.path.join(dir, "gold")
    os.makedirs(data_path, exist_ok=True)
    gold_filepath = os.path.join(data_path, f'test_{test_number}.gold')
    with open(gold_filepath, 'w') as gold_file:
        if add_new_line:
            content = f"{content}\n"
        gold_file.write(content)

    return gold_filepath


class Config():
    """
        Handy class to map traffic_ctl config options.
    """

    def __init__(self, dir, tr, tn):
        self._cmd = "traffic_ctl config "
        self._tr = tr
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

    def describe(self, value):
        self._cmd = f'{self._cmd}  describe {value}'
        return self

    def as_records(self):
        self._cmd = f'{self._cmd} --records'
        return self

    def with_default(self):
        self._cmd = f'{self._cmd}  --default'
        return self

    def __finish(self):
        """
            Sets the command to the test. Make sure this gets called after
            validation is set. Without this call the test will fail.
        """
        self._tr.Processes.Default.Command = self._cmd

    def validate_with_goldfile(self, file: str):
        self._tr.Processes.Default.Streams.stdout = os.path.join("gold", file)
        self.__finish()

    def validate_with_text(self, text: str):
        self._tr.Processes.Default.Streams.stdout = MakeGoldFileWithText(text, self._dir, self._tn)
        self.__finish()


class Server():
    """
        Handy class to map traffic_ctl server options.
    """

    def __init__(self, dir, tr, tn):
        self._cmd = "traffic_ctl server "
        self._tr = tr
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

    def as_json(self):
        self._cmd = f'{self._cmd} -f json'
        return self

    """
    If you need to just run the command with no validation, this is ok in the context of a test, but not to be
    used in isolation(as to run traffic_ctl commands)
    """

    def exec(self):
        self.__finish()

    def __finish(self):
        """
            Sets the command to the test. Make sure this gets called after
            validation is set. Without this call the test will fail.
        """
        self._tr.Processes.Default.Command = self._cmd

    def validate_with_text(self, text: str):
        self._tr.Processes.Default.Streams.stdout = MakeGoldFileWithText(text, self._dir, self._tn)
        self.__finish()


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


def Make_traffic_ctl(test, records_yaml=None):
    tctl = TrafficCtl(test, records_yaml)
    return tctl
