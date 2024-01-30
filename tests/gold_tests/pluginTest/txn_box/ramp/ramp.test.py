# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
import os.path

Test.Summary = '''
Test traffic ramping.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

RepeatCount = 1000

tr = Test.TxnBoxTestAndRun(
    "Ramping",
    "ramp.replay.yaml",
    remap=[('http://one.ex', 'http://three.ex', ('--key=meta.txn_box.remap', 'ramp.replay.yaml'))],
    verifier_client_args="--verbose diag --repeat {}".format(RepeatCount))

with open("{tr.TestDirectory}/multi_ramp_common.pymulti_ramp_common.py") as f:
    code = compile(f.read(), "multi_ramp_common.py", 'exec')
    exec(code)

ts = tr.Variables.TS
ts.Setup.Copy("ramp.replay.yaml", ts.Variables.CONFIGDIR)
ts.Setup.Copy("ramp.logging.yaml", os.path.join(ts.Variables.CONFIGDIR, "logging.yaml"))
ts.Disk.records_config.update({'proxy.config.log.max_secs_per_buffer': 1})
