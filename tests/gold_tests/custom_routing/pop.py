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
import socket
from ports import get_port
import socket
import time

from util import debugOut, addLogging

When = None
Test = None
Testers = None
localhost = socket.gethostname()
start_time = time.clock()


def makeATS(name, origin):
    if (not isinstance(name, str)) and isinstance(name, list):
        return [makeATS(p, origin) for p in name]
    # name -> ts.Name
    # return <- ts

    ts = Test.MakeATSProcess(name)

    ts.Disk.records_config.update({
        'proxy.config.http.insert_response_via_str': 3,
        'proxy.config.http.response_via_str': 3,
        'proxy.config.http.cache.http': 1,
        'proxy.config.http.wait_for_cache': 1,
    })
    ts.Variables.Port = ts.Variables.port

    ts.Env['PROXY_CONFIG_HTTP_SERVER_PORTS'] = "{} {}:ipv6 ".format(ts.Variables.Port, ts.Variables.portv6)

    ts.Disk.remap_config.AddLine(
        'map / http://127.0.0.1:{port}'.format(port=origin.Variables.Port)
    )

    debugOut('makeATS', ts.Name, ts.Variables.Port)
    return ts


def enableRoutingLog(ts):
    if isinstance(ts, list):
        ret = []
        for p in ts:
            ret += [enableRoutingLog(p)]
        return ret
    # log format:
    log_fmt_str = ' '.join([
        '%<cquuc>',                 # client_req_unmapped_url_canonical
        'client=%<chi>:%<chp>',     # client_ip:port
        'cache=%<crc>',             # cache_resp_code
        'nh=%<nhi>:%<nhp>',         # nexthop_ip:port
        'srv_resp_code=%<sssc>',    # server_resp_code
        'srv_txn_cnt=%<sstc>',      # server_transact_count
        # 'srv_via=\"%<{Via}ssh>\"',      # server via header response code
        # 'proxy_via=\"%<{Via}psh>\"',    # proxy via header response code
    ])
    return addLogging(ts, 'routing.log', 'routing', log_fmt_str)

    #test.Disk.File(logfilename, exists=True).Content = Testers.FileContentCallback(g, 'validate_forwards_logged')


def addPort(ts, port_name, ipv6=False):
    if isinstance(ts, list):
        for p in ts:
            addPort(ts, port_name, ipv6)
        return
    get_port(ts, port_name)
    ts.Env['PROXY_CONFIG_HTTP_SERVER_PORTS'] += "{}{}".format(ts.Variables[port_name], ':ipv6' if ipv6 else '')
    debugOut('addPort', [ts.Name, port_name, ts.Variables[port_name]])


def cfgDNS(ts, dns):
    if isinstance(ts, list):
        for p in ts:
            cfgDNS(p, dns)
        return

    debugOut('cfgDNS', ts.Name, dns.Name)
    dns.addRecords(records={ts.Name: ['127.0.0.1', '::{}'.format(ts.Variables.Port)]})
    ts.Disk.records_config.update({
        'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
        'proxy.config.dns.resolv_conf': 'NULL',
        'proxy.config.url_remap.remap_required': 0,
        'proxy.config.http.connect_attempts_timeout': 5
    })


def cfgParents(ts, parents, dest_domain):
    if isinstance(ts, list):
        for p in ts:
            cfgParents(p, parents, dest_domain)
        return

    debugOut('cfgParents', '{} -> {}'.format(ts.Name, [p.Name for p in parents]))

    # update debug tags
    ts.Disk.records_config.update({
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'parent_select|conn'
    })

    # setup parent config
    fname = os.path.join(ts.Variables.CONFIGDIR, "parent.config")
    file = ts.Disk.File(fname, id='parent_config', typename="ats:config")

    # write the config file to map to all nodes in the Pod
    parent_fmt_list = ';'.join([localhost + ':{}'.format(p.Variables.Port) for p in parents])
    config_text = 'dest_domain={} parent="{}" round_robin=consistent_hash'.format(dest_domain, parent_fmt_list)
    file.AddLine(config_text)

    # Each process starts and waits for ready serially.
    # So to wait until all nodes are up and have passed a health check,
    # we will create a test with a custom ready conditions rather than use the process ready feature.
    # ts.Ready = When.FileContains(ts.Disk.diags_log.AbsPath, 'healthcheck status to 1 at time')#, len(peers)-1)

    ts.StartupTimeout = 15
    ts.TimeOut = 150

    debugOut('/cfgParents', [fname, config_text])


def cfgHC(ts, up):
    """Configure the Healthcheck plugin

    Args:
        ts (object): ATS instance
        up (bool): set it to be in an UP state.
    """
    if isinstance(ts, list):
        for p in ts:
            cfgHC(p, up)
        return
    debugOut('cfgHC', [ts, up])
    hc_conf_file = os.path.join(Test.RunDirectory, "hc_{}.config".format(ts.Name))
    status_file = os.path.join(Test.RunDirectory, "status_{}.html".format(ts.Name))
    ts.Disk.plugin_config.AddLine('healthchecks.so ' + hc_conf_file)
    with open(hc_conf_file, 'w') as f:
        f.write('/status.html {} text/plain 200 404'.format(status_file))
        ts.Disk.File(hc_conf_file, id='ats_hc_up', typename="ats:config")
    if up:
        with open(status_file, 'w') as f:
            f.write('some status msg')
            ts.Disk.File(status_file, id='hc_status_{}'.format(ts.Name), typename="ats:config")


def waitAllStarted(tr, proc_list, when_cond=None):
    if not isinstance(proc_list, list):
        proc_list = [proc_list]
    for proc in proc_list:
        tr.Processes.Default.StartBefore(proc, when_cond=when_cond)
    return tr


def checkAllProcessRunning(proc_list):
    tr = Test.AddTestRun("Check all processes still running.")
    tr.StillRunningAfter = proc_list
    tr.TimeOut = 2
    tr.Processes.Default.Command = 'echo ' + tr.Name
    return tr


def cfgResponse(server, dns, host, path, body="", cc="", resp_code=200):
    dns.addRecords(records={host: ['127.0.0.1', '::{}'.format(server.Variables.Port)]})
    request_header = {
        "headers": "GET {} HTTP/1.1\r\nHost: {}\r\n\r\n".format(path, host), "timestamp": "1469733493.993", "body": ""}
    response_header = {"headers": "HTTP/1.1 {} OK\r\nConnection: close\r\nCache-Control: {}\r\n\r\n".format(
        resp_code, cc), "timestamp": "1469733493.993", "body": body}
    server.addResponse("sessionlog.json", request_header, response_header)
