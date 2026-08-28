'''
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
from jsonrpc import Notification, Request, Response

Test.Summary = 'Basic records test. Testing the new records.yaml logic and making sure it works as expected.'

ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update(
    '''
    accept_threads: 1
    cache:
      limits:
        http:
          max_alts: 5
    diags:
      debug:
        enabled: 0
        tags: http|dns
    ''')

# 0 - We want to make sure that the unregistered records are still being detected.
tr = Test.AddTestRun("Test Append value to existing records.yaml")

tr.Processes.Default.Command = 'traffic_ctl config set proxy.config.diags.debug.tags rpc --cold'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.StartBefore(ts)
ts.Disk.records_config.Content = 'gold/records.yaml.cold_test0.gold'

# 1
tr = Test.AddTestRun("Get value from latest added node.")
tr.Processes.Default.Command = 'traffic_ctl config get proxy.config.diags.debug.tags --cold'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env

tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.diags.debug.tags: rpc', 'Config should show the right tags')

# 2
tr = Test.AddTestRun("Test modify latest yaml document from records.yaml")
tr.Processes.Default.Command = 'traffic_ctl config set proxy.config.diags.debug.tags http -u -c'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
ts.Disk.records_config.Content = 'gold/records.yaml.cold_test2.gold'

# 3
tr = Test.AddTestRun("Get value from latest added node 1.")
tr.Processes.Default.Command = 'traffic_ctl config get proxy.config.diags.debug.tags --cold'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env

tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.diags.debug.tags: http', 'Config should show the right tags')

# 4
tr = Test.AddTestRun("Append a new field node using a tag")
tr.Processes.Default.Command = 'traffic_ctl config set proxy.config.cache.limits.http.max_alts 1 -t int -c'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
ts.Disk.records_config.Content = 'gold/records.yaml.cold_test4.gold'

# 5
file = os.path.join(ts.Variables.CONFIGDIR, "new_records.yaml")
tr = Test.AddTestRun("Adding a new node(with update flag set) to a non existing file")
tr.Processes.Default.Command = f'traffic_ctl config set proxy.config.cache.limits.http.max_alts 3  -u -c {file}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Disk.File(file).Content = 'gold/records.yaml.cold_test5.gold'

# 5
file = os.path.join(ts.Variables.CONFIGDIR, "new_records2.yaml")
tr = Test.AddTestRun("Adding a new node to a non existing file")
tr.Processes.Default.Command = f'traffic_ctl config set proxy.config.cache.limits.http.max_alts 3 -c {file}'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Disk.File(file).Content = 'gold/records.yaml.cold_test5.gold'

# --cold takes at most one file name, so it does not consume the record names that follow
# it.  Before that was the case it had to be written after them, which the runs above do.
records_file = os.path.join(ts.Variables.CONFIGDIR, "records.yaml")

# 6
tr = Test.AddTestRun("Get a value with the file name given before the record")
tr.Processes.Default.Command = f'traffic_ctl config get -c {records_file} proxy.config.diags.debug.tags'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.diags.debug.tags: http', 'The record must still be parsed as a record')

# 7
tr = Test.AddTestRun("Get several values with the file name given before them")
tr.Processes.Default.Command = (
    f'traffic_ctl config get -c {records_file} '
    'proxy.config.diags.debug.tags proxy.config.cache.limits.http.max_alts')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.diags.debug.tags: http', 'The first record must be parsed as a record')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.cache.limits.http.max_alts: 1', 'The second record must be parsed as a record')

# 8
tr = Test.AddTestRun("Get a value using the --cold=FILE form")
tr.Processes.Default.Command = f'traffic_ctl config get --cold={records_file} proxy.config.diags.debug.tags'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.diags.debug.tags: http', 'The record must still be parsed as a record')

# 9
file = os.path.join(ts.Variables.CONFIGDIR, "new_records3.yaml")
tr = Test.AddTestRun("Set a value with the file name given before the record and the value")
tr.Processes.Default.Command = f'traffic_ctl config set -c {file} proxy.config.cache.limits.http.max_alts 3'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Disk.File(file).Content = 'gold/records.yaml.cold_test5.gold'

# 10
tr = Test.AddTestRun("--cold takes at most one file name, so repeating it is an error")
tr.Processes.Default.Command = f'traffic_ctl config get --cold={records_file} --cold={records_file} proxy.config.diags.debug.tags'
tr.Processes.Default.ReturnCode = 64  # EX_USAGE - command line usage error
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'at most one argument expected by --cold', 'A repeated --cold must be reported rather than the last one winning')

# 11
tr = Test.AddTestRun("A repeated --cold is an error in the space-separated form too")
tr.Processes.Default.Command = f'traffic_ctl config get -c {records_file} -c {records_file} proxy.config.diags.debug.tags'
tr.Processes.Default.ReturnCode = 64  # EX_USAGE - command line usage error
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'at most one argument expected by --cold', 'A repeated -c must be reported rather than the last one winning')

# 12
tr = Test.AddTestRun("Mixing the two --cold spellings cannot smuggle in a second file name")
tr.Processes.Default.Command = f'traffic_ctl config get -c {records_file} --cold={records_file} proxy.config.diags.debug.tags'
tr.Processes.Default.ReturnCode = 64  # EX_USAGE - command line usage error
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'at most one argument expected by --cold', 'The two forms must be counted together')
