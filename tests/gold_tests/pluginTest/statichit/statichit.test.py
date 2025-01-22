'''
Verify correct statichit plugin behavior
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

import os

Test.Summary = '''
Verify correct statichit plugin behavior
'''

Test.SkipUnless(Condition.PluginExists('statichit.so'),)


class RemapData:
    """Data for each remap rule"""

    @staticmethod
    def setup():
        # Path relative to config directory.  The statichit directory will be created, and small_body.txt copied to it,
        # by the 2nd (dummy) test run.
        RemapData.small_body_path = os.path.join('statichit', 'small_body.txt')

        Test.Setup.Copy('story_16.json', Test.RunDirectory)
        RemapData.json_body_path = os.path.join(Test.RunDirectory, 'story_16.json')

        Test.Setup.Copy('empty.txt', Test.RunDirectory)
        RemapData.empty_body_path = os.path.join(Test.RunDirectory, 'empty.txt')

        # This directory will be created, and a file copied to it, by the 1st (dummy) test run.
        RemapData.dir_path = os.path.join(Test.RunDirectory, 'body_dir')

    def __init__(self, from_url, *args) -> None:
        self.remap_from = from_url
        self.plugin_args = args
        self.body = RemapData.small_body_path

    def bad_body(self):
        self.body = 'invalid'
        return self

    def json_body(self):
        self.body = RemapData.json_body_path
        return self

    def empty_body(self):
        self.body = RemapData.empty_body_path
        return self

    def dir(self):
        self.body = RemapData.dir_path
        return self


RemapData.setup()

remap_data = [
    RemapData('http://fqdn1'),
    RemapData('http://fqdn2', '--max-age=123'),
    RemapData('http://fqdn3', '--success-code=200', '--disable-exact', '--failure-code=222'),
    RemapData('http://fqdn4', '--success-code=200', '--failure-code=412').bad_body(),
    RemapData('http://fqdn5', '--success-code=200', '--failure-code=412', "--mime-type=application/json").json_body(),
    RemapData('http://fqdn6', '--success-code=200', '--failure-code=222').empty_body(),
    RemapData('http://fqdn7', '--success-code', '200', '--disable-exact', '--failure-code', '222'),
    RemapData('http://fqdn8', '-s', '200', '-d', '-c', '222'),
    RemapData('http://fqdn9').dir(),
]

ts = Test.MakeATSProcess('ts')

ts.Disk.records_config.update({
    "proxy.config.diags.debug.enabled": 1,
    "proxy.config.diags.debug.tags": "http|statichit",
})

for d in remap_data:
    arg_str = ''
    for arg in d.plugin_args:
        arg_str += ' @pparam=' + arg
    ts.Disk.remap_config.AddLine(f'map {d.remap_from} http://127.0.0.1/ @plugin=statichit.so @pparam=--file-path={d.body}{arg_str}')

# Dummy test run for copying a file.
tr = Test.AddTestRun()
p = tr.Processes.Default
p.Command = (f'mkdir {Test.RunDirectory}/body_dir ; ' + f'cp {Test.TestDirectory}/small_body.txt {Test.RunDirectory}/body_dir/.')

# Dummy test run for copying a file.
tr = Test.AddTestRun()
p = tr.Processes.Default
p.Command = (
    f'mkdir {ts.Variables.CONFIGDIR}/statichit ; ' + f'cp {Test.TestDirectory}/small_body.txt {ts.Variables.CONFIGDIR}/statichit/.')
p.StartBefore(ts)
p.StillRunningAfter = ts

tr = Test.AddTestRun()
p = tr.AddVerifierClientProcess('client', 'statichit.replay.yaml', http_ports=[ts.Variables.port], other_args='--thread-limit 1')
p.StillRunningAfter = ts
