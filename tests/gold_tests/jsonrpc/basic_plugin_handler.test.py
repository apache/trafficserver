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
import sys
from jsonrpc import Notification, Request, Response

Test.Summary = 'Test JSONRPC Handler inside a plugin'
Test.ContinueOnFail = True
# Define default ATS, we will use the runroot file for traffic_ctl
ts = Test.MakeATSProcess('ts', dump_runroot=True)

Test.testName = 'Basic plugin handler test'

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'rpc|jsonrpc_plugin_handler_test'
    })

# Load plugin
Test.PrepareTestPlugin(
    os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'jsonrpc', 'plugins', '.libs', 'jsonrpc_plugin_handler_test.so'), ts)

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = "echo run jsonrpc_plugin_handler_test plugin"
tr.Processes.Default.ReturnCode = 0
tr.Processes.StillRunningAfter = ts
ts.Ready = 0
ts.Disk.traffic_out.Content = All(
    Testers.IncludesExpression('Test Plugin Initialized.', 'plugin should be properly initialized'),
    Testers.IncludesExpression(
        'test_join_hosts_method successfully registered', 'test_join_hosts_method should be properly registered'),
    Testers.IncludesExpression(
        'test_join_hosts_notification successfully registered', 'test_join_hosts_notification should be properly registered'))

# 1 - now let's try the registered api.
tr = Test.AddTestRun("Test registered API")
tr.AddJsonRPCClientRequest(ts, Request.show_registered_handlers())
tr.DelayStart = 2
# Ok we can just check like this:
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('test_join_hosts_method', 'Should  be listed'),
    Testers.IncludesExpression('test_join_hosts_notification', 'Should be listed'))

# 2 - We perform the same as above but without implicit 'show_registered_handlers()' call.
tr = Test.AddTestRun("Test registered API - using AddJsonRPCShowRegisterHandlerRequest")
tr.AddJsonRPCShowRegisterHandlerRequest(ts)

# Ok we can just check like this:
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('test_join_hosts_method', 'Should  be listed'),
    Testers.IncludesExpression('test_join_hosts_notification', 'Should be listed'))

# 3 - Call the actual plugin method handler:
tr = Test.AddTestRun("Test JSONRPC test_join_hosts_method")
tr.AddJsonRPCClientRequest(ts, Request.test_join_hosts_method(hosts=["yahoo.com", "aol.com", "vz.com"]))


def validate_host_response(resp: Response):
    '''
    Custom check for a particular response values. Also error validation.
    '''
    if resp.is_error():
        return (False, resp.error_as_str())

    join_str = resp.result['join']
    expected = "yahoo.comaol.comvz.com"
    if join_str != expected:
        return (False, f"Invalid response, expected yahoo.comaol.comvz.com, got {join_str}")

    return (True, "All good")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_host_response)

# Custom response validator:


def _validate_response_for_test_io_on_et_task(added, updated, resp: Response):
    '''
    Custom check for a particular response values. Also error validation.
    '''
    if resp.is_error():
        return (False, resp.error_as_str())

    addedHosts = resp.result['addedHosts']
    updatedHosts = resp.result['updatedHosts']

    if addedHosts == added and updatedHosts == updated:
        return (True, "All good")

    return (
        False,
        f"Invalid response, expected addedHosts == {added} and updatedHosts == {updated}, got {addedHosts} and {updatedHosts}")


# 4 - Call plugin handler to perform a IO task on the ET_TASK thread.
tr = Test.AddTestRun("Test test_io_on_et_task")
newHosts = [
    {
        'name': 'brbzull',
        'status': 'up'
    }, {
        'name': 'brbzull1',
        'status': 'down'
    }, {
        'name': 'brbzull3',
        'status': 'up'
    }, {
        'name': 'brbzull4',
        'status': 'down'
    }, {
        'name': 'yahoo',
        'status': 'down'
    }, {
        'name': 'trafficserver',
        'status': 'down'
    }
]

# the jsonrpc query
tr.AddJsonRPCClientRequest(ts, Request.test_io_on_et_task(hosts=newHosts))


def validate_response_for_test_io_on_et_task_1(resp: Response):
    return _validate_response_for_test_io_on_et_task('6', '0', resp)


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_response_for_test_io_on_et_task_1)

# 5
tr = Test.AddTestRun("Test test_io_on_et_task - update, yahoo up")
updateYahoo = [{'name': 'yahoo', 'status': 'up'}]

# the jsonrpc query
tr.AddJsonRPCClientRequest(ts, Request.test_io_on_et_task(hosts=updateYahoo))


def validate_response_for_test_io_on_et_task_2(resp: Response):
    return _validate_response_for_test_io_on_et_task('0', '1', resp)


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_response_for_test_io_on_et_task_2)

# 6
tr = Test.AddTestRun("Test privileged field from plugin handlers")
tr.AddJsonRPCClientRequest(ts, Request.get_service_descriptor())


def validate_privileged_field_for_method(resp: Response, params):
    for (name, val) in params:
        if resp.is_error():
            return (False, resp.error_as_str())

        data = resp.result['methods']
        e = list(filter(lambda x: x['name'] == name, data))[0]
        # Method should be registered.
        if e is None or e['privileged'] != val:
            return (False, f"{name} privileged != {val}")

    return (True, "All good")


def validate_privileged_field(resp: Response):
    return validate_privileged_field_for_method(
        resp, [('test_join_hosts_method', "1"), ('test_io_on_et_task', '1'), ('test_join_hosts_notification', "0")])


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_privileged_field)
