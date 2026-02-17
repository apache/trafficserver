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
import sys
import os

# To include util classes
sys.path.insert(0, f'{Test.TestDirectory}')

from traffic_ctl_test_utils import Make_traffic_ctl
# import ruamel.yaml Uncomment only when GoldFilePathFor is used.


def touch(fname, times=None):
    with open(fname, 'a'):
        os.utime(fname, times)


Test.Summary = '''
Test traffic_ctl config reload.
'''

Test.ContinueOnFail = True

records_config = '''
    udp:
      threads: 1
    diags:
      debug:
        enabled: 1
        tags: rpc|config
        throttling_interval_msec: 0
    '''

traffic_ctl = Make_traffic_ctl(Test, records_config, Any(0, 2))
# todo: we need to get the status just in json format

#### CONFIG STATUS

# Config status with no token, no reloads exist, should return error.
traffic_ctl.config().status().validate_with_text("""
``
Message: No reload tasks found, Code: 6005
``
""")

traffic_ctl.config().status().token("test1").validate_with_text("""
``
Message: Token 'test1' not found, Code: 6001
``
""")

traffic_ctl.config().status().count("all").validate_with_text("""
``
Message: No reload tasks found, Code: 6005
``
""")

traffic_ctl.config().status().token("test1").count("all").validate_with_text(
    """
``
You can't use both --token and --count options together. Ignoring --count
``
Message: Token 'test1' not found, Code: 6001
``
""")
##### CONFIG RELOAD

# basic reload, no params. no existing reload in progress, we expect this to start a new reload.
traffic_ctl.config().reload().validate_with_text("New reload with token '``' was scheduled.")

# basic reload, but traffic_ctl should create and wait for the details, showing the newly created
# reload and some details.
traffic_ctl.config().reload().show_details().validate_with_text(
    """
``
New reload with token '``' was scheduled. Waiting for details...
● Apache Traffic Server Reload [success]
``
""")

# Now we try with a token, this should start a new reload with the given token.
token = "testtoken_1234"
traffic_ctl.config().reload().token(token).validate_with_text(f"New reload with token '{token}' was scheduled.")

# traffic_ctl config status should show the last reload, same as the above.
traffic_ctl.config().status().token(token).validate_with_text(
    """
``
● Apache Traffic Server Reload [success]
   Token     : testtoken_1234
``
""")

# Now we try again, with same token, this should fail as the token already exists.
traffic_ctl.config().reload().token(token).validate_with_text(f"Token '{token}' already exists:")

# Modify ip_allow.yaml and validate the reload status.

tr = Test.AddTestRun("rouch file to trigger ip_allow reload")
tr.Processes.Default.Command = f"touch {os.path.join(traffic_ctl._ts.Variables.CONFIGDIR, 'ip_allow.yaml')}  && sleep 1"
tr.Processes.Default.ReturnCode = 0

traffic_ctl.config().reload().token("reload_ip_allow").show_details().validate_with_text(
    """
``
New reload with token 'reload_ip_allow' was scheduled. Waiting for details...
● Apache Traffic Server Reload [success]
   Token     : reload_ip_allow
``
   Files:
    - ``ip_allow.yaml``  [success]  source: file``
``
""")

##### FORCE RELOAD

# Force reload should work even if we just did a reload
traffic_ctl.config().reload().force().validate_with_text("``New reload with token '``' was scheduled.")

##### INLINE DATA RELOAD

# Test inline data with -d flag (config not registered, expect error but no stuck task)
# Use --force to avoid "reload in progress" conflict
tr = Test.AddTestRun("Inline data reload with unregistered config")
tr.DelayStart = 5  # Wait for previous reload to complete
tr.Processes.Default.Command = f'traffic_ctl config reload --force -d "unknown_cfg: {{foo: bar}}"'
tr.Processes.Default.Env = traffic_ctl._ts.Env
tr.Processes.Default.ReturnCode = Any(0, 1, 2)
tr.StillRunningAfter = traffic_ctl._ts
tr.Processes.Default.Streams.All.Content = Testers.ContainsExpression(
    r'not registered|No configs were scheduled', "Should report config not registered")

# Verify no stuck task - new reload should work immediately after
traffic_ctl.config().reload().token("after_inline_test").validate_with_text(
    "New reload with token 'after_inline_test' was scheduled.")

##### MULTI-KEY FILE RELOAD

# Create a multi-key config file
tr = Test.AddTestRun("Create multi-key config file")
multi_config_path = os.path.join(traffic_ctl._ts.Variables.CONFIGDIR, 'multi_test.yaml')
tr.Processes.Default.Command = f'''cat > {multi_config_path} << 'EOF'
# Multiple config keys in one file
config_a:
  foo: bar
config_b:
  baz: qux
EOF'''
tr.Processes.Default.Env = traffic_ctl._ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = traffic_ctl._ts

# Test reload with multi-key file using data_file()
tr = Test.AddTestRun("Multi-key file reload")
tr.DelayStart = 5  # Wait for previous reload to complete
tr.Processes.Default.Command = f'traffic_ctl config reload --force --data @{multi_config_path}'
tr.Processes.Default.Env = traffic_ctl._ts.Env
tr.Processes.Default.ReturnCode = Any(0, 1, 2)
tr.StillRunningAfter = traffic_ctl._ts
tr.Processes.Default.Streams.All.Content = Testers.ContainsExpression(
    r'not registered|No configs were scheduled|error', "Should process multi-key file")

##### FORCE WITH INLINE DATA

# Force reload with inline data
tr = Test.AddTestRun("Force reload with inline data")
tr.DelayStart = 1
tr.Processes.Default.Command = f'traffic_ctl config reload --force --data "test_config: {{key: value}}"'
tr.Processes.Default.Env = traffic_ctl._ts.Env
tr.Processes.Default.ReturnCode = Any(0, 1, 2)
tr.StillRunningAfter = traffic_ctl._ts
tr.Processes.Default.Streams.All.Content = Testers.ContainsExpression(
    r'not registered|No configs were scheduled|scheduled', "Should handle force with inline data")
