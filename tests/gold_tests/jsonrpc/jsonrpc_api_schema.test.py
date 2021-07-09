'''
JSONRPC Schema test. This test will run a basic request/response schema validation.
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
import tempfile
from string import Template

Test.Summary = 'Test jsonrpc admin API'


# set the schema folder.
schema_folder = os.path.join(Test.TestDirectory, '..', '..', '..', "mgmt2", "rpc", "schema")


def substitute_context_in_file(process, file, context):
    '''
    Perform substitution based on the passed context dict. This function will return a new path for the substituted file.
    '''
    if os.path.isdir(file):
        raise ValueError(f"Mapping substitution not supported for directories.")

    with open(os.path.join(process.TestDirectory, file), 'r') as req_file:
        req_template = Template(req_file.read())
        req_content = req_template.substitute(context)
        tf = tempfile.NamedTemporaryFile(delete=False, dir=process.RunDirectory, suffix=f"_{os.path.basename(file)}")
        file = tf.name
        with open(file, "w") as new_req_file:
            new_req_file.write(req_content)

    return file


def add_testrun_for_jsonrpc_request(
        test_description,
        request_file_name,
        params_schema_file_name=None,
        result_schema_file_name=None,
        context=None):
    '''
    Simple wrapper around the AddJsonRPCClientRequest method.

    context:
      This can be used if a template substitution is needed in the request file.

    stdout_testers:
      Testers to be run on the output stream.

    params_schema_file_name:
        Schema file to validate the request 'params' field.

    result_schema_file_name:
        Schema file to validate the response 'result' field.
    '''
    tr = Test.AddTestRun(test_description)
    tr.Setup.Copy(request_file_name)

    if context:
        request_file_name = substitute_context_in_file(tr, request_file_name, context)

    request_schema_file_name = os.path.join(schema_folder, "jsonrpc_request_schema.json")
    tr.AddJsonRPCClientRequest(
        ts,
        file=os.path.join(
            ts.RunDirectory,
            os.path.basename(request_file_name)),
        schema_file_name=request_schema_file_name,
        params_field_schema_file_name=params_schema_file_name)

    tr.Processes.Default.ReturnCode = 0

    response_schema_file_name = os.path.join(schema_folder, "jsonrpc_response_schema.json")
    tr.Processes.Default.Streams.stdout = Testers.JSONRPCResponseSchemaValidator(
        schema_file_name=response_schema_file_name, result_field_schema_file_name=result_schema_file_name)

    tr.StillRunningAfter = ts
    return tr


ts = Test.MakeATSProcess('ts', enable_cache=True, dump_runroot=True)
# Set TS_RUNROOT, traffic_ctl needs it to find the socket.
ts.SetRunRootEnv()

Test.testName = 'Basic JSONRPC API test'

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'rpc|filemanager|http|cache',
    'proxy.config.jsonrpc.filename': "jsonrpc.yaml",  # We will be using this record to tests some RPC API.
})

# One of the API's will be checking the storage. Need this to get a response with content.
storage_path = os.path.join(Test.RunDirectory, "ts", "storage")
ts.Disk.storage_config.AddLine(f"{storage_path} 512M")


# The following tests will only validate the jsonrpc message, it will not run any validation on the content of the 'result' or 'params'
# of the jsonrpc message. This should be added once the schemas are avilable.

# jsonrpc 2.0 schema file. This will not check the param fields.

success_schema_file_name_name = os.path.join(schema_folder, "success_response_schema.json")
# admin_lookup_records


params_schema_file_name = os.path.join(schema_folder, "admin_lookup_records_params_schema.json")
first = add_testrun_for_jsonrpc_request("Test admin_lookup_records",
                                        request_file_name='json/admin_lookup_records_req_1.json',
                                        params_schema_file_name=params_schema_file_name,
                                        context={'record_name': 'proxy.config.jsonrpc.filename'})
first.Processes.Default.StartBefore(ts)

add_testrun_for_jsonrpc_request("Test admin_lookup_records w/error",
                                request_file_name='json/admin_lookup_records_req_invalid_rec.json')

add_testrun_for_jsonrpc_request("Test admin_lookup_records",
                                request_file_name='json/admin_lookup_records_req_1.json',
                                context={'record_name': 'proxy.config.jsonrpc.filename'})

add_testrun_for_jsonrpc_request("Test admin_lookup_records w/error",
                                request_file_name='json/admin_lookup_records_req_invalid_rec.json')

add_testrun_for_jsonrpc_request("Test admin_lookup_records w/error",
                                request_file_name='json/admin_lookup_records_req_multiple.json',
                                context={'record_name': 'proxy.config.jsonrpc.filename'})

add_testrun_for_jsonrpc_request("Test admin_lookup_records w/error",
                                request_file_name='json/admin_lookup_records_req_metric.json',
                                context={'record_name_regex': 'proxy.process.http.total_client_connections_ipv4*'})


# admin_config_set_records
add_testrun_for_jsonrpc_request("Test admin_lookup_records w/error", request_file_name='json/admin_config_set_records_req.json',
                                context={'record_name': 'proxy.config.jsonrpc.filename', 'record_value': 'test_jsonrpc.yaml'})

# admin_config_reload
add_testrun_for_jsonrpc_request("Test admin_config_reload", request_file_name='json/admin_config_reload_req.json',
                                result_schema_file_name=success_schema_file_name_name)

# admin_clear_metrics_records
add_testrun_for_jsonrpc_request("Clear admin_clear_metrics_records", request_file_name='json/admin_clear_metrics_records_req.json',
                                context={'record_name': 'proxy.process.http.404_responses'})

# admin_host_set_status
add_testrun_for_jsonrpc_request("Test admin_host_set_status", request_file_name='json/admin_host_set_status_req.json',
                                context={'operation': 'up', 'host': 'my.test.host.trafficserver.com'})

# admin_host_set_status
add_testrun_for_jsonrpc_request("Test admin_host_set_status", request_file_name='json/admin_host_set_status_req.json',
                                context={'operation': 'down', 'host': 'my.test.host.trafficserver.com'})


# admin_server_start_drain
add_testrun_for_jsonrpc_request("Test admin_server_start_drain", request_file_name='json/method_call_no_params.json',
                                context={'method': 'admin_server_start_drain'})

add_testrun_for_jsonrpc_request("Test admin_server_start_drain",
                                request_file_name='json/method_call_no_params.json',
                                context={'method': 'admin_server_start_drain'})

# admin_server_stop_drain
add_testrun_for_jsonrpc_request("Test admin_server_stop_drain", request_file_name='json/method_call_no_params.json',
                                context={'method': 'admin_server_stop_drain'})

# admin_storage_get_device_status
add_testrun_for_jsonrpc_request(
    "Test admin_storage_get_device_status",
    request_file_name='json/admin_storage_x_device_status_req.json',
    context={
        'method': 'admin_storage_get_device_status',
        'device': f'{storage_path}/cache.db'})

# admin_storage_set_device_offline
add_testrun_for_jsonrpc_request(
    "Test admin_storage_set_device_offline",
    request_file_name='json/admin_storage_x_device_status_req.json',
    context={
        'method': 'admin_storage_set_device_offline',
        'device': f'{storage_path}/cache.db'})

# admin_plugin_send_basic_msg
add_testrun_for_jsonrpc_request("Test admin_plugin_send_basic_msg", request_file_name='json/admin_plugin_send_basic_msg_req.json',
                                result_schema_file_name=success_schema_file_name_name)
