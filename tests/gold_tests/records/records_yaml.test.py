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
    '''
)

# 0 - We want to make sure that the unregistered records are still being detected.
tr = Test.AddTestRun("Load unregistered records")
tr.Processes.Default.Command = 'echo 1'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

var1 = 'proxy.config.test.not_registered.field1'
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    f"Unrecognized configuration value '{var1}'",
    "Field should be ignored")
var2 = 'proxy.config.test.not_registered.field2'
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    f"Unrecognized configuration value '{var2}",
    "Field should be ignored")


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


tr.AddJsonRPCClientRequest(ts, Request.admin_lookup_records(
    [{"record_name_regex": "proxy.config.test.not_registered.field", "rec_types": ["1", "16"]}]))

# do our own check on the response.
tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(check_response)

# 2
tctl = Test.AddTestRun("Test field with multiplier")
tctl.Processes.Default.Command = 'traffic_ctl config get proxy.config.cache.ram_cache.size'
tctl.Processes.Default.Env = ts.Env
tctl.Processes.Default.ReturnCode = 0
tctl.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    'proxy.config.cache.ram_cache.size: 32212254720',
    'Should hold the configured value.'
)
