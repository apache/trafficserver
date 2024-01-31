# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
import os.path

Test.Summary = '''
Plugin statistics.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))
Test.SkipIf(Condition.true("This needs to be revisit. Metrics seems not to increment properly."))

tr = Test.TxnBoxTestAndRun(
    "Plugin Stats",
    "stat.replay.yaml",
    config_path='Auto',
    config_key='meta.txn_box.global',
    verifier_client_args="--verbose info",
    command="traffic_server")

ts = tr.Variables.TS
ts.Setup.Copy("stat.replay.yaml", ts.Variables.CONFIGDIR)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box',
        'proxy.config.http.cache.http': 0,
        'proxy.config.http.server_ports': '{0}'.format(ts.Variables.port)
    })

probe_r = tr.Variables.TEST.AddTestRun()
probe_r.DelayStart = 20
probe_r.Processes.Default.Command = "traffic_ctl metric get plugin.txn_box.stat-1"
probe_r.Processes.Default.Env = ts.Env
probe_r.Processes.Default.ReturnCode = 0
probe_r.Processes.Default.Streams.stdout = Testers.ContainsExpression("stat-1 3", "Checking stat-1 value")

probe_r = tr.Variables.TEST.AddTestRun()
probe_r.Processes.Default.Command = "traffic_ctl metric get plugin.test.stat-2"
probe_r.Processes.Default.Env = ts.Env
probe_r.Processes.Default.ReturnCode = 0
probe_r.Processes.Default.Streams.stdout = Testers.ContainsExpression("stat-2 105", "Checking stat-2 value")