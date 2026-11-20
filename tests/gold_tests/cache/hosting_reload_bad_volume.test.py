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
Reloading a hosting.config that references undefined volumes must fail the
reload and keep serving with the previous cache host table.
'''

Test.ContinueOnFail = True

PATHS = ("/before", "/after", "/recovered", "/strict")

server = Test.MakeOriginServer("server", ssl=False)
for path in PATHS:
    request = {"headers": f"GET {path} HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
    response = {
        "headers": "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nCache-Control: max-age=300\r\nConnection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "ok"
    }
    server.addResponse("sessionlog.json", request, response)


def write_config(name, lines):
    path = os.path.join(Test.RunDirectory, name)
    with open(path, 'w') as f:
        f.writelines(f'{line}\n' for line in lines)
    return path


empty_hosting = write_config('hosting.config.empty', [])
volumes_1_and_2 = write_config('hosting.config.volumes_1_2', ['hostname=www.example.com volume=2', 'hostname=* volume=1'])
bad_volume_list = write_config('hosting.config.bad_list', ['hostname=* volume=1,99'])


def make_ts(name, storage_yaml=None, hosting=None):
    ts = Test.MakeATSProcess(name, enable_cache=True)
    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'cache_hosting|config',
        })
    ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}')
    if storage_yaml:
        ts.Disk.storage_yaml.AddLine(storage_yaml)
    if hosting:
        ts.Disk.hosting_config.AddLine(hosting)
    # A rejected reload logs an ERROR, so replace the default "no ERROR:" check.
    ts.Disk.diags_log.Content = Testers.ContainsExpression(
        "hosting.config failed to load: .* keeping previous configuration",
        "The rejected hosting.config should not replace the running one")
    ts.Disk.diags_log.Content += Testers.ExcludesExpression("FATAL:", "Diags log file should not contain fatal errors")
    return ts


def add_get(ts, description, path):
    tr = Test.AddTestRun(description)
    tr.Processes.Default.Command = (
        f"curl -s -o /dev/null -w '%{{http_code}}' -H 'Host: www.example.com' "
        f"http://127.0.0.1:{ts.Variables.port}{path}")
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("200", f"GET {path} should be served")
    tr.StillRunningAfter = ts
    tr.StillRunningAfter = server
    return tr


def add_install(ts, description, source):
    tr = Test.AddTestRun(description)
    tr.Processes.Default.Command = f"sleep 1 && cp {source} {os.path.join(ts.Variables.CONFIGDIR, 'hosting.config')}"
    tr.Processes.Default.ReturnCode = 0
    tr.StillRunningAfter = ts
    return tr


def add_reload(ts, expect, description):
    status = "success" if expect == "success" else "fail"
    return Test.AddConfigReload(ts, expect=expect, expect_tasks={"cache_hosting": status}, delay_start=1, description=description)


# Scenario 1: mirror the production incident. No volumes are defined, so only
# the default volume 0 exists and the empty hosting.config places everything in
# the generic volume. The new hosting.config references volumes 1 and 2.
ts = make_ts("ts")
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    r"bad volume number \[1\]", "The new hosting.config should be seen to reference an undefined volume")

tr = add_get(ts, "GET through the cache with the initial hosting.config", "/before")
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts)

add_install(ts, "Install a hosting.config that references undefined volumes 1 and 2", volumes_1_and_2)
add_reload(ts, "fail", "Reload with undefined volumes should be rejected")
add_get(ts, "GET through the cache after the rejected reload", "/after")

add_install(ts, "Restore an empty hosting.config", empty_hosting)
add_reload(ts, "success", "Reload after a rejected reload should succeed")
add_get(ts, "GET through the cache after recovering", "/recovered")

# Scenario 2: the generic volume is valid, so only the strict check can reject
# these files. Volume 1 exists; volumes 2 and 99 do not.
ts_strict = make_ts(
    "ts_strict",
    storage_yaml='''
cache:
  spans:
    - name: disk.0
      path: storage
      size: 256M
  volumes:
    - id: 1
      spans:
        - use: disk.0
          size: 100%
''',
    hosting='hostname=* volume=1')
ts_strict.Disk.diags_log.Content += Testers.ContainsExpression(
    r"bad volume number \[99\]", "A bad volume later in a comma separated list should be reported")

tr = add_get(ts_strict, "GET through the cache with a valid generic volume", "/strict")
tr.Processes.Default.StartBefore(ts_strict)

add_install(ts_strict, "Install a hosting.config whose host line uses undefined volume 2", volumes_1_and_2)
add_reload(ts_strict, "fail", "Reload with a bad host line should be rejected")

add_install(ts_strict, "Install a hosting.config with a bad volume in a list", bad_volume_list)
add_reload(ts_strict, "fail", "Reload with a bad volume in a list should be rejected")

add_get(ts_strict, "GET through the cache after the rejected reloads", "/strict")
