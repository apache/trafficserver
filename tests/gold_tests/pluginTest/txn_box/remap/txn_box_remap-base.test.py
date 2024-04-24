# @file
#
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
#
#  Copyright 2020, Verizon Media
#
import os.path

Test.Summary = '''
Basic remap testing.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

tr = Test.TxnBoxTestAndRun(
    "Remap basics",
    "remap-base.replay.yaml",
    remap=[
        ['http://1.remap.ex/path', 'http://1.remapped.ex', ['--key=meta.txn_box.remap-1', 'remap-base.replay.yaml']],
        ['http://2.remap.ex/path', 'http://2.remapped.ex', ['--key=meta.txn_box.remap-2', 'remap-base.replay.yaml']],
        ['http://3.remap.ex/path', 'http://3.remapped.ex', ['--key=meta.txn_box.remap-3', 'remap-base.replay.yaml']],
        ['http://4.remap.ex/path', 'http://4.remapped.ex', ['--key=meta.txn_box.remap-4', 'remap-base.replay.yaml']],
        ['http://5.remap.ex/path', 'http://5.remapped.ex', ['--key=meta.txn_box.remap-5', 'remap-base.replay.yaml']],
        ['http://base.ex']
    ],
    enable_tls=True)

ts = tr.Variables.TS

ts.Setup.Copy("remap-base.replay.yaml", ts.Variables.CONFIGDIR)  # because it's remap only - not auto-copied.
ts.Setup.Copy("../ssl/server.key", os.path.join(ts.Variables.SSLDir, "server.key"))
ts.Setup.Copy("../ssl/server.pem", os.path.join(ts.Variables.SSLDir, "server.pem"))

ts = tr.Variables.TS
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box',
        'proxy.config.reverse_proxy.enabled': 1

        # enable ssl port
        ,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.http.server_ports': '{0} {1}:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
        'proxy.config.ssl.client.verify.server.policy': "disabled"
    })

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
