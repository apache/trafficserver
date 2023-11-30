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

from jsonrpc import Notification, Request, Response

Test.Summary = 'Basic records test. Testing the new records.yaml logic and making sure it works as expected.'

ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update(
    '''
    diags:
      debug:
        enabled: 1
        tags: rpc|rec
    ssl:
      client:
        cert:
          filename: null
          filenamee: some.txt
          filenam: some2.txt
    dns:
      nameservers: null
    test:
      not_registered:

        # we expect this to be parsed and added to the records but inform that
        # the record field is not registered.
        field1: !!int 1

        # we expect this to fail and report while parsing as the field is not
        # registered nor have a valid type
        # field2: 0 # we cant test this now as traffic_layout will output the error and setup.cli.ext will fail
        # to encode json
        field2: 0

    cache:
      # test multipliers with and without tag
      ram_cache:
        size: 30G
    ''')

# 0 - We want to make sure that the unregistered records are still being detected.
tr = Test.AddTestRun("Load unregistered records")
tr.Processes.Default.Command = 'echo 1'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

var1 = 'proxy.config.test.not_registered.field1'
ts.Disk.diags_log.Content = Testers.ContainsExpression(f"Unrecognized configuration value '{var1}'", "Field should be ignored")
var2 = 'proxy.config.test.not_registered.field2'
ts.Disk.diags_log.Content += Testers.ContainsExpression(f"Unrecognized configuration value '{var2}", "Field should be ignored")

ts.Disk.traffic_out.Content += Testers.ContainsExpression(f"Ignoring field 'filenamee'", "Field should be ignored")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(f"Ignoring field 'filenam'", "Field should be ignored")

# 1
tr = Test.AddTestRun("Query unregistered records.")


def check_response(resp: Response):
    if resp.is_error():
        return (False, resp.error_as_str())

    lst = resp.result['recordList']

    if len(lst) != 2:
        return (False, "Should be 2 records only.")

    for el in lst:
        rec = el['record']
        if rec['registered'] != "false":
            return (False, f"{rec['record_name']} should not be registered")

    return (True, "All good")


tr.AddJsonRPCClientRequest(
    ts, Request.admin_lookup_records([{
        "record_name_regex": "proxy.config.test.not_registered.field",
        "rec_types": ["1", "16"]
    }]))

# do our own check on the response.
tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(check_response)

# 2
tctl = Test.AddTestRun("Test field with multiplier")
tctl.Processes.Default.Command = 'traffic_ctl config get proxy.config.cache.ram_cache.size'
tctl.Processes.Default.Env = ts.Env
tctl.Processes.Default.ReturnCode = 0
tctl.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    'proxy.config.cache.ram_cache.size: 32212254720', 'Should hold the configured value.')

# The whole idea is to test how ATS handles having multiple docs in the same records.yaml
# file.
# Every subsequent document will overwrite the previous one, this tests are meant to
# exercise that logic.

ts2 = Test.MakeATSProcess("ts2")

# We are adding a new doc at the end of the original one created by the unit test.
ts2.Disk.records_config.append_to_document('''
    diags:
      debug:
        enabled: 0
        tags: rpc|rec
    ''')

# This will append a new doc in the same file, this config value should overwrite
# the previous one.
ts2.Disk.records_config.append_to_document('''
    diags:
      debug:
        tags: filemanager
    ''')

# After all the loading completed we want to have enabled=0 and tags=filemanager

tr = Test.AddTestRun("Start a new ATS instance.")
tr.Processes.Default.Command = 'echo 1'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts2)
tr.Processes.StillRunningAfter = ts2

tr2 = Test.AddTestRun("Test multiple docs from the same file")
tr2.Processes.Default.Command = 'traffic_ctl config get proxy.config.diags.debug.enabled proxy.config.diags.debug.tags'
tr2.Processes.Default.Env = ts2.Env
tr2.Processes.Default.ReturnCode = 0

# Make sure it's what we want.
tr2.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.diags.debug.enabled: 0', 'Config should show debug disabled')
tr2.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.diags.debug.tags: filemanager', 'Config should show a different tag')

ts2.Disk.records_config.append_to_document(
    '''
    dns:
      resolv_conf: NULL
      nameservers: null
      local_ipv6: Null
    ssl:
      client:
        cert:
          filename: ~
    ''')

tr3 = Test.AddTestRun("test null string")
tr3.Processes.Default.Command = 'traffic_ctl config get  proxy.config.dns.resolv_conf proxy.config.dns.local_ipv6 proxy.config.dns.nameservers proxy.config.ssl.client.cert.filename'
tr3.Processes.Default.Env = ts2.Env
tr3.Processes.Default.ReturnCode = 0

# Make sure it's what we want.
tr3.Processes.Default.Streams.stdout += Testers.ContainsExpression('proxy.config.dns.resolv_conf: null', 'should be set to null')
tr3.Processes.Default.Streams.stdout += Testers.ContainsExpression('proxy.config.dns.nameservers: null', 'should be set to null')
tr3.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.ssl.client.cert.filename: null', 'should be set to null')
tr3.Processes.Default.Streams.stdout += Testers.ContainsExpression('proxy.config.dns.local_ipv6: null', 'should be set to null')

ts3 = Test.MakeATSProcess("ts3")
ts3.Disk.records_config.update('''
    some_invalid_field_should_not_block_further_docs_from_the_parser_logic: OK
    ''')

ts3.Disk.records_config.append_to_document('''
    diags:
      debug:
        tags: rpc|rec
    ''')

# We want to make sure that any error in any of the docs will not stop ATS from loading
# the rest of the nodes.
tr4 = Test.AddTestRun("test parsing multiple docs with invalid fields.")
tr4.Processes.Default.Command = 'traffic_ctl config get proxy.config.diags.debug.tags'
tr4.Processes.Default.Env = ts3.Env
tr.Processes.Default.StartBefore(ts3)
tr.Processes.StillRunningAfter = ts3
tr4.Processes.Default.ReturnCode = 0

# This record should be parsed after the invalid field. Value should be the one
# configured.
tr4.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.diags.debug.tags: rpc|rec', 'should be the configured value')
