# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Basic TLS testing.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

tr = Test.TxnBoxTestAndRun(
    "Basic TLS",
    "tls.replay.yaml",
    remap=[
        ['http://base.ex/', ('--key=meta.txn_box.remap', 'tls.replay.yaml')],
        ['https://base.ex/', ('--key=meta.txn_box.remap', 'tls.replay.yaml')]
    ],
    verifier_client_args="--verbose info",
    enable_tls=True)

ts = tr.Variables.TS

ts.Setup.Copy("tls.replay.yaml", ts.Variables.CONFIGDIR)  # because it's remap only - not auto-copied.
ts.addDefaultSSLFiles()

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box|http|ssl',
        'proxy.config.http.cache.http': 0,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir)
        # enable ssl port
        ,
        'proxy.config.http.server_ports': '{0} {1}:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
        'proxy.config.ssl.client.verify.server.policy': 'DISABLED'
    })
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
