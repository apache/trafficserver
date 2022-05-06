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
Test remap reloading
'''
Test.testName = 'remap_reload'

replay_file_1 = "reload_1.replay.yaml"
replay_file_2 = "reload_2.replay.yaml"
replay_file_3 = "reload_3.replay.yaml"
replay_file_4 = "reload_4.replay.yaml"

tm = Test.MakeATSProcess("tm", command="traffic_manager", select_ports=True)
tm.Disk.diags_log.Content = Testers.ContainsExpression("remap.config failed to load", "Remap should fail to load")
remap_cfg_path = os.path.join(tm.Variables.CONFIGDIR, 'remap.config')

pv = Test.MakeVerifierServerProcess("pv", "reload_server.replay.yaml")
pv_port = pv.Variables.http_port
tm.Disk.remap_config.AddLines([f"map http://alpha.ex http://alpha.ex:{pv_port}",
                               f"map http://bravo.ex http://bravo.ex:{pv_port}",
                               f"map http://charlie.ex http://charlie.ex:{pv_port}",
                               f"map http://delta.ex http://delta.ex:{pv_port}"])
tm.Disk.records_config.update({'proxy.config.url_remap.min_rules_required': 3})

nameserver = Test.MakeDNServer("dns", default='127.0.0.1')
tm.Disk.records_config.update({
    'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}", 'proxy.config.dns.resolv_conf': 'NULL'
})

tr_1 = Test.AddTestRun("verify load")
tr_1.Processes.Default.StartBefore(pv)
tr_1.Processes.Default.StartBefore(nameserver)
tr_1.Processes.Default.StartBefore(tm)
tr_1.AddVerifierClientProcess("client", replay_file_1, http_ports=[tm.Variables.port])

tr_2 = Test.AddTestRun("reload-fail")
tr_2.Processes.Default.Command = 'traffic_ctl config reload'
tr_2.Processes.Default.Env = tm.Env
tr_2.Disk.File(remap_cfg_path).WriteOn("")
tr_2.Disk.File(remap_cfg_path, typename="ats:config").AddLines([
    f"map http://alpha.ex http://alpha.ex:{pv_port}", f"map http://bravo.ex http://bravo.ex:{pv_port}"
])

tr_3 = Test.AddTestRun("pause")
tr_3.Processes.Default.Command = 'sleep 5'

tr_4 = Test.AddTestRun("after first reload")
tr_4.AddVerifierClientProcess("client_2", replay_file_2, http_ports=[tm.Variables.port])

tr_5 = Test.AddTestRun("reload-succeed")
tr_5.Processes.Default.Command = 'traffic_ctl config reload'
tr_5.Processes.Default.Env = tm.Env
tr_5.Disk.File(remap_cfg_path).WriteOn("")
tr_5.Disk.File(remap_cfg_path,
               typename="ats:config").AddLines([f"map http://echo.ex http://echo.ex:{pv_port}",
                                                f"map http://foxtrot.ex http://foxtrot.ex:{pv_port}",
                                                f"map http://golf.ex http://golf.ex:{pv_port}",
                                                f"map http://hotel.ex http://hotel.ex:{pv_port}",
                                                f"map http://india.ex http://india.ex:{pv_port}"])

tr_6 = Test.AddTestRun("pause")
tr_6.Processes.Default.Command = 'sleep 5'

tr_7 = Test.AddTestRun("post update charlie")
tr_7.AddVerifierClientProcess("client_3", replay_file_3, http_ports=[tm.Variables.port])

tr_8 = Test.AddTestRun("post update golf")
tr_8.AddVerifierClientProcess("client_4", replay_file_4, http_ports=[tm.Variables.port])
