'''
Test ip_allow and ip_categories reload via ConfigRegistry.

Verifies that:
1. ip_allow.yaml touch triggers ip_allow reload
2. ip_categories touch triggers ip_allow reload (via add_file_dependency)
3. Unrelated config touch (hosting.config) does NOT trigger ip_allow reload
4. ip_categories content change causes actual behavior change after reload
5. Changing ip_categories record value (traffic_ctl config set) triggers ip_allow reload
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
import shutil

Test.Summary = '''
Test ip_allow and ip_categories reload via ConfigRegistry add_file_dependency.
'''

Test.ContinueOnFail = True

# --- Setup: origin server ---
server = Test.MakeOriginServer("server", ssl=False)
request = {"headers": "GET /test HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response = {
    "headers": "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "ok"
}
server.addResponse("sessionlog.json", request, response)

# --- Setup: ip_categories file variants ---

# Version A: 127.0.0.1 is INTERNAL (requests allowed)
categories_allow = os.path.join(Test.RunDirectory, 'categories_allow.yaml')
with open(categories_allow, 'w') as f:
    f.write('ip_categories:\n  - name: INTERNAL\n    ip_addrs: 127.0.0.1\n')

# Version B: 127.0.0.1 is NOT INTERNAL (GET denied, only HEAD allowed by catch-all)
categories_deny = os.path.join(Test.RunDirectory, 'categories_deny.yaml')
with open(categories_deny, 'w') as f:
    f.write('ip_categories:\n  - name: INTERNAL\n    ip_addrs: 1.2.3.4\n')

# Version C: 127.0.0.1 back in INTERNAL (for record value change test)
categories_restore = os.path.join(Test.RunDirectory, 'categories_restore.yaml')
with open(categories_restore, 'w') as f:
    f.write('ip_categories:\n  - name: INTERNAL\n    ip_addrs: 127.0.0.1\n')

# Active ip_categories file that the record points to (start with allow version)
categories_file = os.path.join(Test.RunDirectory, 'ip_categories.yaml')
shutil.copy(categories_allow, categories_file)

# --- Setup: ATS ---
ts = Test.MakeATSProcess("ts", enable_cache=True)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ip_allow|config',
        'proxy.config.cache.ip_categories.filename': categories_file,
    })

# ip_allow config:
#   Rule 1: INTERNAL category → allow ALL methods
#   Rule 2: catch-all 0/0 → allow only HEAD
#
# Effect:
#   127.0.0.1 in INTERNAL  → GET /test → 200 (rule 1 matches)
#   127.0.0.1 NOT in INTERNAL → GET /test → 403 (rule 2 matches, GET not allowed)
ts.Disk.ip_allow_yaml.AddLines(
    '''ip_allow:
  - apply: in
    ip_categories: INTERNAL
    action: allow
    methods: ALL
  - apply: in
    ip_addrs: 0/0
    action: allow
    methods:
      - HEAD
'''.split("\n"))

ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}')

config_dir = ts.Variables.CONFIGDIR
reload_counter = 0

# ================================================================
# Test 1: Touch ip_allow.yaml → reload → ip_allow reloads (count 2)
# ================================================================

tr = Test.AddTestRun("Touch ip_allow.yaml")
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = f"sleep 3 && touch {os.path.join(config_dir, 'ip_allow.yaml')} && sleep 1"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

reload_counter += 1
tr = Test.AddTestRun("Reload after ip_allow.yaml touch")
p = tr.Processes.Process(f"reload-{reload_counter}")
p.Command = 'traffic_ctl config reload; sleep 30'
p.Env = ts.Env
p.ReturnCode = Any(0, -2)
p.Ready = When.FileContains(ts.Disk.diags_log.Name, "ip_allow.yaml finished loading", 1 + reload_counter)
p.Timeout = 20
tr.Processes.Default.StartBefore(p)
tr.Processes.Default.Command = 'echo "waiting for ip_allow reload after ip_allow.yaml touch"'
tr.TimeOut = 25
tr.StillRunningAfter = ts

# ================================================================
# Test 2: Touch ip_categories → reload → ip_allow reloads (count 3)
#         Verifies add_file_dependency() correctly wired ip_categories
#         to trigger ip_allow reload via FileManager mtime detection.
# ================================================================

tr = Test.AddTestRun("Touch ip_categories")
tr.Processes.Default.Command = f"touch {categories_file} && sleep 1"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

reload_counter += 1
tr = Test.AddTestRun("Reload after ip_categories touch")
p = tr.Processes.Process(f"reload-{reload_counter}")
p.Command = 'traffic_ctl config reload; sleep 30'
p.Env = ts.Env
p.ReturnCode = Any(0, -2)
p.Ready = When.FileContains(ts.Disk.diags_log.Name, "ip_allow.yaml finished loading", 1 + reload_counter)
p.Timeout = 20
tr.Processes.Default.StartBefore(p)
tr.Processes.Default.Command = 'echo "waiting for ip_allow reload after ip_categories touch"'
tr.TimeOut = 25
tr.StillRunningAfter = ts

# ================================================================
# Test 3: Touch hosting.config → reload → ip_allow NOT triggered
#         Verifies the fix for the false trigger bug where changing
#         any file in the config directory spuriously triggered
#         ip_allow reload (due to FileManager watching the directory
#         instead of ip_categories.yaml when the record was "").
# ================================================================

tr = Test.AddTestRun("Touch hosting.config and reload (should NOT trigger ip_allow)")
tr.Processes.Default.Command = (
    f"touch {os.path.join(config_dir, 'hosting.config')} && "
    f"sleep 1 && "
    f"traffic_ctl config reload && "
    f"sleep 5")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = Any(0, -2)
tr.Processes.Default.Timeout = 15
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify ip_allow loaded exactly 3 times (no false trigger)")
tr.DelayStart = 3
tr.Processes.Default.Command = (f"grep -c 'ip_allow.yaml finished loading' {ts.Disk.diags_log.Name} "
                                f"| grep -qx 3")
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ================================================================
# Test 4: Functional — change ip_categories content, verify behavior
#         Proves that add_file_dependency() not only triggers the
#         reload but that the handler actually re-reads the updated
#         ip_categories file.
# ================================================================

# 4a: Verify initial state: GET → 200 (127.0.0.1 is in INTERNAL)
tr = Test.AddTestRun("GET should succeed (127.0.0.1 in INTERNAL category)")
tr.Processes.Default.Command = (f"curl -s -o /dev/null -w '%{{http_code}}' "
                                f"http://127.0.0.1:{ts.Variables.port}/test")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("200", "Should get 200 when 127.0.0.1 is in INTERNAL category")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 4b: Swap ip_categories to deny version (127.0.0.1 NOT in INTERNAL)
tr = Test.AddTestRun("Change ip_categories to deny 127.0.0.1")
tr.Processes.Default.Command = f"cp {categories_deny} {categories_file}"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# 4c: Reload and wait for ip_allow to pick up the change
reload_counter += 1
tr = Test.AddTestRun("Reload after ip_categories content change")
p = tr.Processes.Process(f"reload-{reload_counter}")
p.Command = 'traffic_ctl config reload; sleep 30'
p.Env = ts.Env
p.ReturnCode = Any(0, -2)
p.Ready = When.FileContains(ts.Disk.diags_log.Name, "ip_allow.yaml finished loading", 1 + reload_counter)
p.Timeout = 20
tr.Processes.Default.StartBefore(p)
tr.Processes.Default.Command = 'echo "waiting for reload after ip_categories content change"'
tr.TimeOut = 25
tr.StillRunningAfter = ts

# 4d: GET should now be denied (falls to catch-all: only HEAD allowed)
tr = Test.AddTestRun("GET should be denied (127.0.0.1 NOT in INTERNAL)")
tr.Processes.Default.Command = (f"curl -s -o /dev/null -w '%{{http_code}}' "
                                f"http://127.0.0.1:{ts.Variables.port}/test")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("403", "Should get 403 when 127.0.0.1 is not in INTERNAL category")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# ================================================================
# Test 5: Record value change triggers ip_allow reload
#         Changes the ip_categories filename record via traffic_ctl
#         config set (no explicit config reload). The callback
#         registered by add_file_dependency() fires when the record
#         value changes, triggering ip_allow reload with the new file.
# ================================================================

# 5a: Change the ip_categories filename record to point to categories_restore
#     (which has 127.0.0.1 back in INTERNAL).
#     No traffic_ctl config reload — the RecRegisterConfigUpdateCb fires
#     automatically via config_update_cont when the record value changes.
reload_counter += 1
tr = Test.AddTestRun("Change ip_categories record value to new file")
p = tr.Processes.Process(f"reload-{reload_counter}")
p.Command = (f"traffic_ctl config set proxy.config.cache.ip_categories.filename "
             f"'{categories_restore}'; sleep 30")
p.Env = ts.Env
p.ReturnCode = Any(0, -2)
p.Ready = When.FileContains(ts.Disk.diags_log.Name, "ip_allow.yaml finished loading", 1 + reload_counter)
p.Timeout = 20
tr.Processes.Default.StartBefore(p)
tr.Processes.Default.Command = 'echo "waiting for ip_allow reload after record value change"'
tr.TimeOut = 25
tr.StillRunningAfter = ts

# 5b: GET should succeed again (new file has 127.0.0.1 in INTERNAL)
tr = Test.AddTestRun("GET should succeed after record value change")
tr.Processes.Default.Command = (f"curl -s -o /dev/null -w '%{{http_code}}' "
                                f"http://127.0.0.1:{ts.Variables.port}/test")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "200", "Should get 200 after restoring INTERNAL category via record change")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
