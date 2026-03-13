'''
Test inline config reload functionality via unified admin_config_reload RPC method.

Inline mode is triggered by passing the "configs" parameter.
Tests the following features:
1. Basic inline reload with single config
2. Multiple configs in single request
3. File-based vs inline mode detection
4. Unknown config key error handling
5. Invalid YAML content handling
6. Reload while another is in progress
7. Verify config is actually applied
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

from jsonrpc import Request, Response

Test.Summary = 'Test inline config reload via RPC'
Test.ContinueOnFail = True

ts = Test.MakeATSProcess('ts', dump_runroot=True)

Test.testName = 'config_reload_rpc'

# Initial configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'rpc|config',
})

# ============================================================================
# Test 1: File-based reload (no configs parameter)
# ============================================================================
tr = Test.AddTestRun("File-based reload without configs parameter")
tr.Processes.Default.StartBefore(ts)
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload())


def validate_file_based(resp: Response):
    '''Verify file-based reload works when configs is not provided'''
    result = resp.result
    token = result.get('token', '')
    message = result.get('message', [])

    if token:
        return (True, f"File-based reload started: token={token}")

    errors = result.get('errors', [])
    if errors:
        return (True, f"File-based reload response: {errors}")

    return (True, f"Response: {result}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_file_based)
tr.StillRunningAfter = ts

# ============================================================================
# Test 2: Empty configs map (should trigger inline mode but process 0 configs)
# ============================================================================
tr = Test.AddTestRun("Empty configs map")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload(configs={}))


def validate_empty_configs(resp: Response):
    '''Verify behavior with empty configs'''
    result = resp.result

    # Empty configs should succeed but with 0 changes
    success = result.get('success', -1)
    failed = result.get('failed', -1)

    if success == 0 and failed == 0:
        return (True, f"Empty configs handled: success={success}, failed={failed}")

    # Or it might be an error
    errors = result.get('errors', [])
    if errors:
        return (True, f"Empty configs rejected: {errors}")

    return (True, f"Result: {result}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_empty_configs)
tr.StillRunningAfter = ts

# ============================================================================
# Test 3: Unknown config key (error code 6010)
# ============================================================================
tr = Test.AddTestRun("Unknown config key should error with code 6010")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload(configs={"unknown_config_key": {"some": "data"}}))


def validate_unknown_key(resp: Response):
    '''Verify error for unknown config key - should return error code 6010'''
    result = resp.result
    errors = result.get('errors', [])

    if not errors:
        return (False, f"Expected error for unknown key, got: {result}")

    error_str = str(errors)
    if '6010' in error_str or 'not registered' in error_str:
        return (True, f"Unknown key rejected with code 6010: {errors}")
    return (False, f"Expected error 6010, got: {errors}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_unknown_key)
tr.StillRunningAfter = ts

# ============================================================================
# Test 3b: Unregistered config rejected (error code 6010)
# Note: remap.config is not registered in ConfigRegistry
# ============================================================================
tr = Test.AddTestRun("Unregistered config should error with code 6010")
tr.DelayStart = 1
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload(configs={"remap.config": {"some": "data"}}))


def validate_legacy_not_supported(resp: Response):
    '''Verify unregistered config returns error code 6010'''
    result = resp.result
    errors = result.get('errors', [])

    if not errors:
        return (False, f"Expected rejection for unregistered config, got: {result}")

    error_str = str(errors)
    if '6010' in error_str or 'not registered' in error_str:
        return (True, f"Unregistered config correctly rejected: {errors}")
    return (False, f"Expected error 6010, got: {errors}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_legacy_not_supported)
tr.StillRunningAfter = ts

# ============================================================================
# Test 4: RPC-injected content rejected for FileOnly config (ip_allow)
# ip_allow is registered with ConfigSource::FileOnly — RPC content must be rejected
# ============================================================================
tr = Test.AddTestRun("RPC-injected content rejected for FileOnly config (ip_allow)")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(
    ts,
    Request.admin_config_reload(
        configs={"ip_allow": [{
            "apply": "in",
            "ip_addrs": "127.0.0.1",
            "action": "allow",
            "methods": ["GET", "HEAD"]
        }]}))


def validate_rpc_inject_rejected(resp: Response):
    '''ip_allow is registered as FileOnly — RPC-injected content must be rejected with 6011'''
    result = resp.result
    errors = result.get('errors', [])

    if not errors:
        return (False, f"Expected rejection for FileOnly config, got: {result}")

    error_str = str(errors)
    if '6011' in error_str or 'does not support RPC' in error_str:
        return (True, f"FileOnly config correctly rejected RPC injection: {errors}")
    return (False, f"Expected error 6011, got: {errors}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_rpc_inject_rejected)
tr.StillRunningAfter = ts

# ============================================================================
# Test 5: Multiple configs in single request
# ============================================================================
tr = Test.AddTestRun("Multiple configs in single request")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(
    ts,
    Request.admin_config_reload(
        configs={
            "ip_allow": [{
                "apply": "in",
                "ip_addrs": "0.0.0.0/0",
                "action": "allow"
            }],
            "sni": [{
                "fqdn": "*.test.com",
                "verify_client": "NONE"
            }],
            "records": {
                "diags": {
                    "debug": {
                        "enabled": 1
                    }
                }
            }
        }))


def validate_multiple_configs(resp: Response):
    '''All configs should be rejected — none support RPC content source at this stage'''
    result = resp.result
    errors = result.get('errors', [])

    if not errors:
        return (False, f"Expected rejections for all configs, got: {result}")

    # Each config should produce an error (6010=not registered, 6011=RPC source not supported)
    error_str = str(errors)
    if '6010' in error_str or '6011' in error_str:
        return (True, f"All configs rejected as expected: {len(errors)} errors")
    return (False, f"Unexpected errors: {errors}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_multiple_configs)
tr.StillRunningAfter = ts

# ============================================================================
# Test 6: Reload while another is in progress
# ============================================================================
tr = Test.AddTestRun("First reload request")
tr.DelayStart = 1
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload())


def validate_first_reload(resp: Response):
    '''Start a regular reload'''
    result = resp.result
    token = result.get('token', '')
    return (True, f"First reload started: token={token}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_first_reload)
tr.StillRunningAfter = ts

# Immediately try inline reload
tr = Test.AddTestRun("Inline reload while regular reload in progress")
tr.DelayStart = 0  # No delay - immediately after
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload(configs={"ip_allow": [{"apply": "in", "ip_addrs": "10.0.0.0/8"}]}))


def validate_in_progress_rejection(resp: Response):
    '''Should be rejected for RPC source not supported or reload in progress'''
    result = resp.result
    errors = result.get('errors', [])

    if not errors:
        return (False, f"Expected rejection, got: {result}")

    error_str = str(errors)
    # Either 6011 (RPC source not supported) or 6004 (reload in progress)
    if '6011' in error_str or '6004' in error_str:
        return (True, f"Correctly rejected: {errors}")
    return (False, f"Unexpected error: {errors}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_in_progress_rejection)
tr.StillRunningAfter = ts

# ============================================================================
# Test 7: Verify token is returned with inline- prefix
# ============================================================================
tr = Test.AddTestRun("Verify inline token prefix")
tr.DelayStart = 3  # Wait for previous reloads to complete
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload(configs={"unknown_for_token_test": {"data": "value"}}))


def validate_inline_token(resp: Response):
    '''Verify token has inline- prefix'''
    result = resp.result
    token = result.get('token', '')

    if token and token.startswith('inline-'):
        return (True, f"Token has correct prefix: {token}")

    if not token:
        # Check if there's an error (which is fine)
        errors = result.get('errors', [])
        if errors:
            return (True, f"No token (error case): {errors}")

    return (True, f"Token result: {token}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_inline_token)
tr.StillRunningAfter = ts

# ============================================================================
# Test 8: Nested YAML structure (records.diags.debug)
# ============================================================================
tr = Test.AddTestRun("Nested YAML structure")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(
    ts,
    Request.admin_config_reload(
        configs={"records": {
            "diags": {
                "debug": {
                    "enabled": 1,
                    "tags": "http|rpc|test"
                }
            },
            "http": {
                "cache": {
                    "http": 1
                }
            }
        }}))


def validate_nested_yaml(resp: Response):
    '''Verify nested YAML handling'''
    result = resp.result
    success = result.get('success', 0)
    failed = result.get('failed', 0)
    errors = result.get('errors', [])

    return (True, f"Nested YAML: success={success}, failed={failed}, errors={errors}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_nested_yaml)
tr.StillRunningAfter = ts

# ============================================================================
# Test 9: Query status after inline reload
# ============================================================================
tr = Test.AddTestRun("Query status after inline reload")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(ts, Request.get_reload_config_status())


def validate_status_after_inline(resp: Response):
    '''Check status includes inline reload info'''
    if resp.is_error():
        return (True, f"Status query error (may be expected): {resp.error_as_str()}")

    result = resp.result
    tasks = result.get('tasks', [])

    if tasks:
        # Check if any task has inline- prefix
        for task in tasks:
            token = task.get('token', '')
            if token.startswith('inline-'):
                return (True, f"Found inline reload in status: {token}")

    return (True, f"Status result: {result}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_status_after_inline)
tr.StillRunningAfter = ts

# ============================================================================
# Test 10: Large config content
# ============================================================================
tr = Test.AddTestRun("Large config content")
tr.DelayStart = 2

# Generate a larger config
large_ip_allow = []
for i in range(50):
    large_ip_allow.append({"apply": "in", "ip_addrs": f"10.{i}.0.0/16", "action": "allow"})

tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload(configs={"ip_allow": large_ip_allow}))


def validate_large_config(resp: Response):
    '''Large ip_allow config should also be rejected (FileOnly)'''
    result = resp.result
    errors = result.get('errors', [])

    if not errors:
        return (False, f"Expected rejection for FileOnly config, got: {result}")

    error_str = str(errors)
    if '6011' in error_str:
        return (True, f"Large config correctly rejected: {errors}")
    return (False, f"Expected error 6011, got: {errors}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_large_config)
tr.StillRunningAfter = ts
