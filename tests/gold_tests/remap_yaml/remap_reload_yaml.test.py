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


def update_remap_yaml(path: str, lines: list) -> None:
    """Update the remap.yaml file.

    This is used to update the config file between test runs without
    triggering framework warnings about overriding file objects.

    :param path: The path to the remap.config file.
    :param lines: The list of lines to write to the file.
    """
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


Test.Summary = '''
Test remap reloading
'''
Test.testName = 'remap_reload'

replay_file_1 = "reload_1.replay.yaml"
replay_file_2 = "reload_2.replay.yaml"
replay_file_3 = "reload_3.replay.yaml"
replay_file_4 = "reload_4.replay.yaml"

tm = Test.MakeATSProcess("ts")
tm.Disk.diags_log.Content = Testers.ContainsExpression("remap.yaml failed to load", "Remap should fail to load")
remap_cfg_path = os.path.join(tm.Variables.CONFIGDIR, 'remap.yaml')

pv = Test.MakeVerifierServerProcess("pv", "reload_server.replay.yaml")
pv_port = pv.Variables.http_port

tm.Disk.remap_yaml.AddLines(
    f'''
remap:
  - type: map
    from:
      url: http://alpha.ex
    to:
      url: http://alpha.ex:{pv_port}
  - type: map
    from:
      url: http://bravo.ex
    to:
      url: http://bravo.ex:{pv_port}
  - type: map
    from:
      url: http://charlie.ex
    to:
      url: http://charlie.ex:{pv_port}
  - type: map
    from:
      url: http://delta.ex
    to:
      url: http://delta.ex:{pv_port}
    '''.split("\n"))

tm.Disk.records_config.update({'proxy.config.url_remap.min_rules_required': 3})

nameserver = Test.MakeDNServer("dns", default='127.0.0.1')
tm.Disk.records_config.update(
    {
        'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}",
        'proxy.config.dns.resolv_conf': 'NULL'
    })

tr = Test.AddTestRun("verify load")
tr.Processes.Default.StartBefore(pv)
tr.Processes.Default.StartBefore(nameserver)
tr.Processes.Default.StartBefore(tm)
tr.AddVerifierClientProcess("client", replay_file_1, http_ports=[tm.Variables.port])

tr = Test.AddTestRun("Change remap.yaml to have only two remap rules")
p = tr.Processes.Default
p.Env = tm.Env
p.Command = 'echo "Change remap.yaml, two lines"'
p.Setup.Lambda(
    lambda: update_remap_yaml(
        remap_cfg_path, f'''
remap:
  - type: map
    from:
      url: http://alpha.ex
    to:
      url: http://alpha.ex:{pv_port}
  - type: map
    from:
      url: http://bravo.ex
    to:
      url: http://bravo.ex:{pv_port}
    '''.split("\n")))

tr = Test.AddTestRun("remap_yaml reload, fails")
tr.Processes.Default.Env = tm.Env
tr.Processes.Default.Command = 'sleep 2; traffic_ctl config reload'

tr = Test.AddTestRun("after first reload")
await_config_reload = tr.Processes.Process('config_reload_failed', 'sleep 30')
await_config_reload.Ready = When.FileContains(tm.Disk.diags_log.Name, "configuration is invalid")
tr.AddVerifierClientProcess("client_2", replay_file_2, http_ports=[tm.Variables.port])
tr.Processes.Default.StartBefore(await_config_reload)

tr = Test.AddTestRun("Change remap.yaml to have more than three remap rules")
p = tr.Processes.Default
p.Env = tm.Env
p.Command = 'echo "Change remap.yaml, more than three lines"'
p.Setup.Lambda(
    lambda: update_remap_yaml(
        remap_cfg_path, f'''
remap:
  - type: map
    from:
      url: http://echo.ex
    to:
      url: http://echo.ex:{pv_port}
  - type: map
    from:
      url: http://foxtrot.ex
    to:
      url: http://foxtrot.ex:{pv_port}
  - type: map
    from:
      url: http://golf.ex
    to:
      url: http://golf.ex:{pv_port}
  - type: map
    from:
      url: http://hotel.ex
    to:
      url: http://hotel.ex:{pv_port}
  - type: map
    from:
      url: http://india.ex
    to:
      url: http://india.ex:{pv_port}
    '''.split("\n")))

tr = Test.AddTestRun("remap_yaml reload, succeeds")
tr.Processes.Default.Env = tm.Env
tr.Processes.Default.Command = 'sleep 2; traffic_ctl config reload'

tr = Test.AddTestRun("post update charlie")
await_config_reload = tr.Processes.Process('config_reload_succeeded', 'sleep 30')
await_config_reload.Ready = When.FileContains(tm.Disk.diags_log.Name, "remap.yaml finished loading", 2)
tr.Processes.Default.StartBefore(await_config_reload)
tr.AddVerifierClientProcess("client_3", replay_file_3, http_ports=[tm.Variables.port])

tr = Test.AddTestRun("post update golf")
tr.AddVerifierClientProcess("client_4", replay_file_4, http_ports=[tm.Variables.port])
