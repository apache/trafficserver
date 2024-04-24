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
Verify txn_box can filter fields as expected.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

r = Test.TxnBoxTestAndRun(
    "Test HTTP field manipulation",
    "ct_header.replay.yaml",
    config_path='Auto',
    config_key="meta.txn_box",
    remap=[
        ["http://base.ex/"], ["https://base.ex/"], ["https://u.protected.ex/"], ["http://s.protected.ex/"],
        ["https://s.protected.ex/"], ["https://protected.ex/"]
    ],
    enable_tls=True)
ts = r.Variables.TS

ts.Setup.Copy("../ssl/server.key", os.path.join(ts.Variables.SSLDir, "server.key"))
ts.Setup.Copy("../ssl/server.pem", os.path.join(ts.Variables.SSLDir, "server.pem"))

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1
        #    , 'proxy.config.diags.debug.tags': 'txn_box|http|ssl'
        ,
        'proxy.config.diags.debug.tags': 'txn_box',
        'proxy.config.http.cache.http': 0,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir)
        # enable ssl port
        ,
        'proxy.config.http.server_ports': '{0} {1}:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
        'proxy.config.ssl.client.verify.server.policy': 'DISABLED'
    })
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
