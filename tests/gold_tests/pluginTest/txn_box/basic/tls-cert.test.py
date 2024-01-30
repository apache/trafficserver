# @file
#
# Copyright 2022, Apache Software Foundation
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Test certificate handling.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

tr = Test.TxnBoxTestAndRun(
    "TLS Certs",
    "tls-cert.replay.yaml",
    config_path='Auto',
    config_key="meta.txn_box.global",
    enable_tls=True,
    remap=[
        ['https://alpha.ex/', "https://alpha.ex/"], ['http://alpha.ex/', 'https://alpha.ex/'],
        ['http://charlie.ex/', 'https://charlie.ex/']
    ])

ts = tr.Variables.TS

ts.Setup.Copy("../ssl/server.key", os.path.join(ts.Variables.SSLDir, "server.key"))
ts.Setup.Copy("../ssl/server.pem", os.path.join(ts.Variables.SSLDir, "server.pem"))
ts.Setup.Copy("../ssl/bravo-signed.cert", os.path.join(ts.Variables.SSLDir, "bravo-signed.cert"))

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box|http|ssl',
        'proxy.config.http.cache.http': 0,
        'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir
        # enable ssl port
        ,
        'proxy.config.http.server_ports': '{0} {1}:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
        'proxy.config.ssl.client.certification_level': 0,
        'proxy.config.ssl.client.verify.server.policy': 'DISABLED',
        'proxy.config.ssl.client.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.client.cert.filename': "bravo-signed.cert"
    })
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
