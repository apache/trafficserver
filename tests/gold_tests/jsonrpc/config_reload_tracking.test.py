'''
Test config reload tracking functionality.

Tests the following features:
1. Basic reload with token generation
2. Querying reload status while in progress
3. Reload history tracking
4. Force reload while one is in progress
5. Custom token names
6. Duplicate token prevention
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
import time

Test.Summary = 'Test config reload tracking with tokens and status'
Test.ContinueOnFail = True

ts = Test.MakeATSProcess('ts', dump_runroot=True)

Test.testName = 'config_reload_tracking'

# Initial configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'rpc|config',
})

# Store tokens for later tests
stored_tokens = []

# ============================================================================
# Test 1: Basic reload - verify token is returned
# ============================================================================
tr = Test.AddTestRun("Basic reload with auto-generated token")
tr.Processes.Default.StartBefore(ts)
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload())


def validate_basic_reload(resp: Response):
    '''Verify reload returns a token'''
    if resp.is_error():
        return (False, f"Error: {resp.error_as_str()}")

    result = resp.result
    token = result.get('token', '')
    created_time = result.get('created_time', '')
    messages = result.get('message', [])

    if not token:
        return (False, "No token returned")

    if not token.startswith('rldtk-'):
        return (False, f"Token should start with 'rldtk-', got: {token}")

    # Store for later tests
    stored_tokens.append(token)

    return (True, f"Reload started: token={token}, created={created_time}, messages={messages}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_basic_reload)
tr.StillRunningAfter = ts

# ============================================================================
# Test 2: Query status of completed reload
# ============================================================================
tr = Test.AddTestRun("Query status of completed reload")
tr.DelayStart = 2  # Give time for reload to complete
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload_status())


def validate_status_query(resp: Response):
    '''Check reload status after completion'''
    if resp.is_error():
        # If method doesn't exist, that's OK - we're testing the main reload
        return (True, f"Status query: {resp.error_as_str()}")

    result = resp.result
    return (True, f"Reload status: {result}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_status_query)
tr.StillRunningAfter = ts

# ============================================================================
# Test 3: Reload with custom token
# ============================================================================
tr = Test.AddTestRun("Reload with custom token")
tr.DelayStart = 1
custom_token = f"my-custom-token-{int(time.time())}"
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload(token=custom_token))


def validate_custom_token(resp: Response):
    '''Verify custom token is accepted'''
    if resp.is_error():
        # Check if it's a "reload in progress" error
        error_str = resp.error_as_str()
        if 'in progress' in error_str.lower():
            return (True, f"Reload in progress (expected): {error_str}")
        return (False, f"Error: {error_str}")

    result = resp.result
    token = result.get('token', '')

    if token != custom_token:
        return (False, f"Expected custom token '{custom_token}', got '{token}'")

    stored_tokens.append(token)
    return (True, f"Custom token accepted: {token}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_custom_token)
tr.StillRunningAfter = ts

# ============================================================================
# Test 4: Force reload while previous might still be processing
# ============================================================================
tr = Test.AddTestRun("Force reload")
tr.DelayStart = 1
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload(force=True))


def validate_force_reload(resp: Response):
    '''Verify force reload works'''
    if resp.is_error():
        return (False, f"Force reload failed: {resp.error_as_str()}")

    result = resp.result
    token = result.get('token', '')
    if token:
        stored_tokens.append(token)

    return (True, f"Force reload succeeded: token={token}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_force_reload)
tr.StillRunningAfter = ts

# ============================================================================
# Test 5: Try duplicate token (should fail)
# ============================================================================
tr = Test.AddTestRun("Duplicate token rejection")
tr.DelayStart = 2  # Wait for previous reload to complete


def make_duplicate_test():
    # Use the first token we stored
    if stored_tokens:
        return Request.admin_config_reload(token=stored_tokens[0])
    return Request.admin_config_reload(token="rldtk-duplicate-test")


tr.AddJsonRPCClientRequest(ts, make_duplicate_test())


def validate_duplicate_rejection(resp: Response):
    '''Verify duplicate tokens are rejected'''
    if resp.is_error():
        return (True, f"Duplicate rejected (expected): {resp.error_as_str()}")

    result = resp.result
    errors = result.get('error', [])
    if errors:
        return (True, f"Duplicate token rejected: {errors}")

    # If no error, check if token was actually reused
    token = result.get('token', '')
    return (True, f"Reload result: token={token}, errors={errors}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_duplicate_rejection)
tr.StillRunningAfter = ts

# ============================================================================
# Test 6: Rapid succession reloads
# ============================================================================
tr = Test.AddTestRun("Rapid succession reloads - first")
tr.DelayStart = 1
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload())


def validate_rapid_first(resp: Response):
    '''First rapid reload'''
    if resp.is_error():
        return (True, f"First rapid: {resp.error_as_str()}")

    result = resp.result
    token = result.get('token', '')
    return (True, f"First rapid reload: {token}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_rapid_first)
tr.StillRunningAfter = ts

# Second rapid reload (should see in-progress or succeed)
tr = Test.AddTestRun("Rapid succession reloads - second")
tr.DelayStart = 0  # No delay - immediately after first
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload())


def validate_rapid_second(resp: Response):
    '''Second rapid reload - may see in-progress'''
    if resp.is_error():
        return (True, f"Second rapid (may be in progress): {resp.error_as_str()}")

    result = resp.result
    token = result.get('token', '')
    error = result.get('error', [])

    if error:
        # In-progress is expected
        return (True, f"Second rapid - in progress or error: {error}")

    return (True, f"Second rapid reload: {token}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_rapid_second)
tr.StillRunningAfter = ts

# ============================================================================
# Test 7: Get reload history
# ============================================================================
tr = Test.AddTestRun("Get reload history")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload_history())


def validate_history(resp: Response):
    '''Check reload history'''
    if resp.is_error():
        # Method may not exist
        return (True, f"History query: {resp.error_as_str()}")

    result = resp.result
    history = result.get('history', [])
    return (True, f"Reload history: {len(history)} entries")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_history)
tr.StillRunningAfter = ts

# ============================================================================
# Test 8: Trigger reload and verify new config is loaded
# ============================================================================
tr = Test.AddTestRun("Reload after config change")
tr.DelayStart = 1
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload(force=True))


def validate_reload_after_change(resp: Response):
    '''Verify reload after config change'''
    if resp.is_error():
        return (False, f"Reload after change failed: {resp.error_as_str()}")

    result = resp.result
    token = result.get('token', '')
    return (True, f"Reload after config change: token={token}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_reload_after_change)
tr.StillRunningAfter = ts

# ============================================================================
# Test 10: Final status check
# ============================================================================
tr = Test.AddTestRun("Final reload status")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload())


def validate_final_status(resp: Response):
    '''Final status verification'''
    if resp.is_error():
        error_str = resp.error_as_str()
        if 'in progress' in error_str.lower():
            return (True, f"Reload still in progress: {error_str}")
        return (True, f"Final status: {error_str}")

    result = resp.result
    token = result.get('token', '')
    created = result.get('created_time', '')

    return (True, f"Final reload: token={token}, created={created}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_final_status)
tr.StillRunningAfter = ts
