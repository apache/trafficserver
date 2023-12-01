'''
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
import re

Test.Summary = 'Test the STEK Share plugin'
Test.testName = "stek_share"

Test.SkipUnless(Condition.PluginExists('stek_share.so'))

server = Test.MakeOriginServer('server')

ts1 = Test.MakeATSProcess("ts1", select_ports=True, enable_tls=True)
ts2 = Test.MakeATSProcess("ts2", select_ports=True, enable_tls=True)
ts3 = Test.MakeATSProcess("ts3", select_ports=True, enable_tls=True)
ts4 = Test.MakeATSProcess("ts4", select_ports=True, enable_tls=True)
ts5 = Test.MakeATSProcess("ts5", select_ports=True, enable_tls=True)

Test.Setup.Copy('ssl/self_signed.crt')
Test.Setup.Copy('ssl/self_signed.key')
Test.Setup.Copy('server_list.yaml')

cert_path = os.path.join(Test.RunDirectory, 'self_signed.crt')
key_path = os.path.join(Test.RunDirectory, 'self_signed.key')
server_list_path = os.path.join(Test.RunDirectory, 'server_list.yaml')

request_header1 = {'headers': 'GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n', 'timestamp': '1469733493.993', 'body': ''}
response_header1 = {'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n', 'timestamp': '1469733493.993', 'body': 'curl test'}
server.addResponse('sessionlog.json', request_header1, response_header1)

stek_share_conf_path_1 = os.path.join(ts1.Variables.CONFIGDIR, 'stek_share_conf.yaml')
stek_share_conf_path_2 = os.path.join(ts2.Variables.CONFIGDIR, 'stek_share_conf.yaml')
stek_share_conf_path_3 = os.path.join(ts3.Variables.CONFIGDIR, 'stek_share_conf.yaml')
stek_share_conf_path_4 = os.path.join(ts4.Variables.CONFIGDIR, 'stek_share_conf.yaml')
stek_share_conf_path_5 = os.path.join(ts5.Variables.CONFIGDIR, 'stek_share_conf.yaml')

ts1.Disk.File(stek_share_conf_path_1, id="stek_share_conf_1", typename="ats:config")
ts2.Disk.File(stek_share_conf_path_2, id="stek_share_conf_2", typename="ats:config")
ts3.Disk.File(stek_share_conf_path_3, id="stek_share_conf_3", typename="ats:config")
ts4.Disk.File(stek_share_conf_path_4, id="stek_share_conf_4", typename="ats:config")
ts5.Disk.File(stek_share_conf_path_5, id="stek_share_conf_5", typename="ats:config")

ts1.Disk.stek_share_conf_1.AddLines(
    [
        'server_id: 1',
        'address: 127.0.0.1',
        'port: 10001',
        'asio_thread_pool_size: 4',
        'heart_beat_interval: 100',
        'election_timeout_lower_bound: 200',
        'election_timeout_upper_bound: 400',
        'reserved_log_items: 5',
        'snapshot_distance: 5',
        'client_req_timeout: 3000',  # this is in milliseconds
        'key_update_interval: 3600',  # this is in seconds
        'server_list_file: {0}'.format(server_list_path),
        'root_cert_file: {0}'.format(cert_path),
        'server_cert_file: {0}'.format(cert_path),
        'server_key_file: {0}'.format(key_path),
        'cert_verify_str: /C=US/ST=IL/O=Yahoo/OU=Edge/CN=stek-share',
    ])

ts2.Disk.stek_share_conf_2.AddLines(
    [
        'server_id: 2',
        'address: 127.0.0.1',
        'port: 10002',
        'asio_thread_pool_size: 4',
        'heart_beat_interval: 100',
        'election_timeout_lower_bound: 200',
        'election_timeout_upper_bound: 400',
        'reserved_log_items: 5',
        'snapshot_distance: 5',
        'client_req_timeout: 3000',  # this is in milliseconds
        'key_update_interval: 3600',  # this is in seconds
        'server_list_file: {0}'.format(server_list_path),
        'root_cert_file: {0}'.format(cert_path),
        'server_cert_file: {0}'.format(cert_path),
        'server_key_file: {0}'.format(key_path),
        'cert_verify_str: /C=US/ST=IL/O=Yahoo/OU=Edge/CN=stek-share',
    ])

ts3.Disk.stek_share_conf_3.AddLines(
    [
        'server_id: 3',
        'address: 127.0.0.1',
        'port: 10003',
        'asio_thread_pool_size: 4',
        'heart_beat_interval: 100',
        'election_timeout_lower_bound: 200',
        'election_timeout_upper_bound: 400',
        'reserved_log_items: 5',
        'snapshot_distance: 5',
        'client_req_timeout: 3000',  # this is in milliseconds
        'key_update_interval: 3600',  # this is in seconds
        'server_list_file: {0}'.format(server_list_path),
        'root_cert_file: {0}'.format(cert_path),
        'server_cert_file: {0}'.format(cert_path),
        'server_key_file: {0}'.format(key_path),
        'cert_verify_str: /C=US/ST=IL/O=Yahoo/OU=Edge/CN=stek-share',
    ])

ts4.Disk.stek_share_conf_4.AddLines(
    [
        'server_id: 4',
        'address: 127.0.0.1',
        'port: 10004',
        'asio_thread_pool_size: 4',
        'heart_beat_interval: 100',
        'election_timeout_lower_bound: 200',
        'election_timeout_upper_bound: 400',
        'reserved_log_items: 5',
        'snapshot_distance: 5',
        'client_req_timeout: 3000',  # this is in milliseconds
        'key_update_interval: 3600',  # this is in seconds
        'server_list_file: {0}'.format(server_list_path),
        'root_cert_file: {0}'.format(cert_path),
        'server_cert_file: {0}'.format(cert_path),
        'server_key_file: {0}'.format(key_path),
        'cert_verify_str: /C=US/ST=IL/O=Yahoo/OU=Edge/CN=stek-share',
    ])

ts5.Disk.stek_share_conf_5.AddLines(
    [
        'server_id: 5',
        'address: 127.0.0.1',
        'port: 10005',
        'asio_thread_pool_size: 4',
        'heart_beat_interval: 100',
        'election_timeout_lower_bound: 200',
        'election_timeout_upper_bound: 400',
        'reserved_log_items: 5',
        'snapshot_distance: 5',
        'client_req_timeout: 3000',  # this is in milliseconds
        'key_update_interval: 3600',  # this is in seconds
        'server_list_file: {0}'.format(server_list_path),
        'root_cert_file: {0}'.format(cert_path),
        'server_cert_file: {0}'.format(cert_path),
        'server_key_file: {0}'.format(key_path),
        'cert_verify_str: /C=US/ST=IL/O=Yahoo/OU=Edge/CN=stek-share',
    ])

ts1.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'stek_share',
        'proxy.config.exec_thread.autoconfig': 0,
        'proxy.config.exec_thread.limit': 4,
        'proxy.config.ssl.server.cert.path': '{0}'.format(Test.RunDirectory),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(Test.RunDirectory),
        'proxy.config.ssl.session_cache': 2,
        'proxy.config.ssl.session_cache.size': 1024,
        'proxy.config.ssl.session_cache.timeout': 7200,
        'proxy.config.ssl.session_cache.num_buckets': 16,
        'proxy.config.ssl.server.session_ticket.enable': 1,
        'proxy.config.ssl.server.cipher_suite':
            'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA'
    })
ts1.Disk.plugin_config.AddLine('stek_share.so {0}'.format(stek_share_conf_path_1))
ts1.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=self_signed.crt ssl_key_name=self_signed.key')
ts1.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts2.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'stek_share',
        'proxy.config.exec_thread.autoconfig': 0,
        'proxy.config.exec_thread.limit': 4,
        'proxy.config.ssl.server.cert.path': '{0}'.format(Test.RunDirectory),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(Test.RunDirectory),
        'proxy.config.ssl.session_cache': 2,
        'proxy.config.ssl.session_cache.size': 1024,
        'proxy.config.ssl.session_cache.timeout': 7200,
        'proxy.config.ssl.session_cache.num_buckets': 16,
        'proxy.config.ssl.server.session_ticket.enable': 1,
        'proxy.config.ssl.server.cipher_suite':
            'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA'
    })
ts2.Disk.plugin_config.AddLine('stek_share.so {0}'.format(stek_share_conf_path_2))
ts2.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=self_signed.crt ssl_key_name=self_signed.key')
ts2.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts3.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'stek_share',
        'proxy.config.exec_thread.autoconfig': 0,
        'proxy.config.exec_thread.limit': 4,
        'proxy.config.ssl.server.cert.path': '{0}'.format(Test.RunDirectory),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(Test.RunDirectory),
        'proxy.config.ssl.session_cache': 2,
        'proxy.config.ssl.session_cache.size': 1024,
        'proxy.config.ssl.session_cache.timeout': 7200,
        'proxy.config.ssl.session_cache.num_buckets': 16,
        'proxy.config.ssl.server.session_ticket.enable': 1,
        'proxy.config.ssl.server.cipher_suite':
            'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA'
    })
ts3.Disk.plugin_config.AddLine('stek_share.so {0}'.format(stek_share_conf_path_3))
ts3.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=self_signed.crt ssl_key_name=self_signed.key')
ts3.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts4.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'stek_share',
        'proxy.config.exec_thread.autoconfig': 0,
        'proxy.config.exec_thread.limit': 4,
        'proxy.config.ssl.server.cert.path': '{0}'.format(Test.RunDirectory),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(Test.RunDirectory),
        'proxy.config.ssl.session_cache': 2,
        'proxy.config.ssl.session_cache.size': 1024,
        'proxy.config.ssl.session_cache.timeout': 7200,
        'proxy.config.ssl.session_cache.num_buckets': 16,
        'proxy.config.ssl.server.session_ticket.enable': 1,
        'proxy.config.ssl.server.cipher_suite':
            'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA'
    })
ts4.Disk.plugin_config.AddLine('stek_share.so {0}'.format(stek_share_conf_path_4))
ts4.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=self_signed.crt ssl_key_name=self_signed.key')
ts4.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts5.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'stek_share',
        'proxy.config.exec_thread.autoconfig': 0,
        'proxy.config.exec_thread.limit': 4,
        'proxy.config.ssl.server.cert.path': '{0}'.format(Test.RunDirectory),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(Test.RunDirectory),
        'proxy.config.ssl.session_cache': 2,
        'proxy.config.ssl.session_cache.size': 1024,
        'proxy.config.ssl.session_cache.timeout': 7200,
        'proxy.config.ssl.session_cache.num_buckets': 16,
        'proxy.config.ssl.server.session_ticket.enable': 1,
        'proxy.config.ssl.server.cipher_suite':
            'ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA'
    })
ts5.Disk.plugin_config.AddLine('stek_share.so {0}'.format(stek_share_conf_path_5))
ts5.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=self_signed.crt ssl_key_name=self_signed.key')
ts5.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))


def check_session(ev, test):
    retval = False
    f = open(test.GetContent(ev), 'r')
    err = "Session ids match"
    if not f:
        err = "Failed to open {0}".format(openssl_output)
        return (retval, "Check that session ids match", err)

    content = f.read()
    match = re.findall('Session-ID: ([0-9A-F]+)', content)

    if match:
        if all(i == j for i, j in zip(match, match[1:])):
            err = "{0} reused successfully {1} times".format(match[0], len(match) - 1)
            retval = True
        else:
            err = "Session is not being reused as expected"
    else:
        err = "Didn't find session id"
    return (retval, "Check that session ids match", err)


tr1 = Test.AddTestRun('Basic Curl test, and give it enough time for all ATS to start up and sync STEK')
tr1.Processes.Default.Command = 'sleep 10 && curl https://127.0.0.1:{0} -k'.format(ts1.Variables.ssl_port)
tr1.Processes.Default.ReturnCode = 0
tr1.Processes.Default.StartBefore(server)
tr1.Processes.Default.StartBefore(ts1)
tr1.Processes.Default.StartBefore(ts2)
tr1.Processes.Default.StartBefore(ts3)
tr1.Processes.Default.StartBefore(ts4)
tr1.Processes.Default.StartBefore(ts5)
tr1.Processes.Default.Streams.All = Testers.ContainsExpression('curl test', 'Making sure the basics still work')
ts1.Disk.traffic_out.Content = Testers.ContainsExpression('Generate initial STEK succeeded', 'should succeed')
ts1.Disk.traffic_out.Content = Testers.ContainsExpression('Generate initial STEK succeeded', 'should succeed')
ts1.Disk.traffic_out.Content = Testers.ContainsExpression('Generate initial STEK succeeded', 'should succeed')
ts1.Disk.traffic_out.Content = Testers.ContainsExpression('Generate initial STEK succeeded', 'should succeed')
ts1.Disk.traffic_out.Content = Testers.ContainsExpression('Generate initial STEK succeeded', 'should succeed')
tr1.StillRunningAfter = server
tr1.StillRunningAfter += ts1
tr1.StillRunningAfter += ts2
tr1.StillRunningAfter += ts3
tr1.StillRunningAfter += ts4
tr1.StillRunningAfter += ts5

tr2 = Test.AddTestRun("TLSv1.2 Session Ticket")
tr2.Command = \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -sess_out {5} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{0} -sess_in {5} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{1} -sess_in {5} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{2} -sess_in {5} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{3} -sess_in {5} && ' \
    'echo -e "GET / HTTP/1.1\r\n" | openssl s_client -tls1_2 -connect 127.0.0.1:{4} -sess_in {5}' \
    .format(
        ts1.Variables.ssl_port,
        ts2.Variables.ssl_port,
        ts3.Variables.ssl_port,
        ts4.Variables.ssl_port,
        ts5.Variables.ssl_port,
        os.path.join(Test.RunDirectory, 'sess.dat')
    )
tr2.ReturnCode = 0
tr2.Processes.Default.Streams.All.Content = Testers.Lambda(check_session)
