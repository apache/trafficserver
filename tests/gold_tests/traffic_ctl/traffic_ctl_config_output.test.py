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
# import ruamel.yaml Uncomment only when GoldFilePathFor is used.

Test.Summary = '''
Test traffic_ctl config output responses.
'''

Test.ContinueOnFail = True

TestNumber = 0


def IncTestNumber():
    global TestNumber
    TestNumber = TestNumber + 1


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


def MakeGoldFileWithText(content, add_new_line=True):
    data_path = os.path.join(Test.TestDirectory, "gold")
    os.makedirs(data_path, exist_ok=True)
    gold_filepath = os.path.join(data_path, f'test_{TestNumber}.gold')
    with open(gold_filepath, 'w') as gold_file:
        if add_new_line:
            content = f"{content}\n"
        gold_file.write(content)

    return gold_filepath


class Config():
    """
        Handy class to map traffic_ctl config options.
    """

    def __init__(self, tr):
        self._cmd = "traffic_ctl config "
        self._tr = tr

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
        self._tr.Processes.Default.Streams.stdout = MakeGoldFileWithText(text)
        self.__finish()


class TrafficCtl(Config):
    """
        Single TS instance with multiple tests.
        Every time a config() is called, a new test is created.
    """

    def __init__(self, records_yaml=None):
        self._current_test_number = TestNumber

        self._ts = Test.MakeATSProcess(f"ts_{TestNumber}")
        if records_yaml != None:
            self._ts.Disk.records_config.update(records_yaml)
        self._tests = []

    def __get_index(self):
        return self._current_test_number

    def add_test(self):

        tr = Test.AddTestRun(f"test {TestNumber}")
        if TestNumber == 0:
            tr.Processes.Default.StartBefore(self._ts)
        IncTestNumber()

        tr.Processes.Default.Env = self._ts.Env
        tr.DelayStart = 3
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._ts

        self._tests.insert(self.__get_index(), tr)
        return self

    def config(self):
        self.add_test()
        return Config(self._tests[self.__get_index()])


def Make_traffic_ctl(records_yaml):
    tctl = TrafficCtl(records_yaml)
    return tctl


records_yaml = '''
    udp:
      threads: 1
    diags:
      debug:
        enabled: 1
        tags: rpc
        throttling_interval_msec: 0
    '''

traffic_ctl = Make_traffic_ctl(records_yaml)

##### CONFIG GET

# YAML output
traffic_ctl.config().get("proxy.config.diags.debug.tags").as_records().validate_with_goldfile("t1_yaml.gold")
# Default output
traffic_ctl.config().get("proxy.config.diags.debug.enabled").validate_with_text("proxy.config.diags.debug.enabled: 1")
# Default output with default.
traffic_ctl.config().get("proxy.config.diags.debug.tags").with_default() \
    .validate_with_text("proxy.config.diags.debug.tags: rpc # default http|dns")

# Now same output test but with defaults, traffic_ctl supports adding default value
# when using --records.
traffic_ctl.config().get("proxy.config.diags.debug.tags").as_records().with_default().validate_with_goldfile("t2_yaml.gold")
traffic_ctl.config().get(
    "proxy.config.diags.debug.tags proxy.config.diags.debug.enabled proxy.config.diags.debug.throttling_interval_msec").as_records(
    ).with_default().validate_with_goldfile("t3_yaml.gold")

##### CONFIG MATCH
traffic_ctl.config().match("threads").with_default().validate_with_goldfile("match.gold")

# The idea is to check the traffic_ctl yaml emitter when a value starts with the
# same prefix of a node like:
# diags:
#    logfile:
#    logfile_perm: rw-r--r--
#
# traffic_ctl have a special logic to deal with cases like this, so better test it.
traffic_ctl.config().match("diags.logfile").as_records().validate_with_goldfile("t4_yaml.gold")

##### CONFIG DIFF
traffic_ctl.config().diff().validate_with_goldfile("diff.gold")
traffic_ctl.config().diff().as_records().validate_with_goldfile("diff_yaml.gold")

##### CONFIG DESCRIBE
# don't really care about values, but just output and that the command actually went through
traffic_ctl.config().describe("proxy.config.http.server_ports").validate_with_goldfile("describe.gold")
