'''
Additional tests for TS plugin API.
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
import yaml

Test.Summary = '''
Additional tests for TS plugin API.
'''

# Synthetic client/server regression tests from InkAPITest.cc moved to here.

Test.GetTcpPort("HOOKS_src_port")


class Txn:
    """
    An HTTP GET Transaction (request and response).
    """

    def __init__(
        self, id, req_mime_fields=None, userver_resp_fields=None, userver_resp_body="", use_userver_host=True,
        client_src_port=None, userver_url=None, duplicate_of=None, last_in_wave=False
    ):
        """
        id - a string, unique to this transaction.
        req_mime_fields - list of string, request MIME fields.
        userver_resp_fields - list of string, userver response MIME field lines (no userver object entry if
                              None).
        userver_resp_body - string, body of userver response.
        userver_url - string, URL for userver.  If None, defaults to /ID, where ID is the id parameter.
        use_userver_host - boolean, if True host in request is 127.0.0.1:userver-port, otherwise invalid host
                           specifier.
        client_src_port - specifice source port for ncat (if any).
        duplicate_of - If not None, must be the string id of the transaction this one will be a duplicate of.
                       If req_mime_fields is None, it defaults to req_mime_fields of the duplicate transaction.
                       The parameters unserver_resp_lines, userver_resp_body and use_userver_host are
                       don't-cares.
        last_in_wave - boolean, must be true if transaction is the last one in a wave.  The sublist of
                       transactions in a particular wave execute in parallel.  All the transactions in a wave
                       must execute before the transactions in the next wave are started.
        """
        self.id = id
        self.duplicate_of = duplicate_of
        self.req_mime_fields = req_mime_fields
        if None is duplicate_of:
            self.userver_resp_fields = userver_resp_fields
            self.userver_resp_body = userver_resp_body
            self.use_userver_host = use_userver_host
            self.userver_url = userver_url
        self.client_src_port = client_src_port
        self.last_in_wave = last_in_wave


# List of transactions exeuted by this test.
#
txns = [
    Txn(
        id="HOOKS",
        req_mime_fields=[
            "X-Request-ID: 1"
        ],
        userver_resp_fields=[
            "HTTP/1.1 200 OK",
            "X-Response-ID: 1",
            "Content-Type: text/html",
            "Cache-Control: no-cache"
        ],
        userver_resp_body="Body for response 1",
        client_src_port=Test.Variables.HOOKS_src_port
    ),
    Txn(
        id="CACHE",
        req_mime_fields=[
            "X-Request-ID: 2"
        ],
        userver_resp_fields=[
            "HTTP/1.1 200 OK",
            "X-Response-ID: 2",
            "Cache-Control: max-age=86400",
            "Content-Type: text/html"
        ],
        userver_resp_body="Body for response 2"
    ),
    Txn(
        id="SSN",
        req_mime_fields=[
            "X-Request-ID: 3",
            "Response: Error"
        ]
    ),
    Txn(
        id="TRANSFORM1",
        req_mime_fields=[
            "X-Request-ID: 4",
            "Request:1"
        ],
        userver_resp_fields=[
            "HTTP/1.1 200 OK",
            "X-Response-ID: 4",
            "Cache-Control: max-age=86400",
            "Content-Type: text/html"
        ],
        userver_resp_body="Body for response 4",
    ),
    Txn(
        id="ALT_INFO1",
        req_mime_fields=[
            "X-Request-ID: 6",
            "Accept-Language: English"
        ],
        userver_resp_fields=[
            "HTTP/1.1 200 OK",
            "X-Response-ID: 6",
            "Cache-Control: max-age=86400",
            "Content-Language: English"
        ],
        userver_resp_body="Body for response 6"
    ),
    Txn(
        id="PARENT_PROXY",
        req_mime_fields=[
            "X-Request-ID: 11"
        ],
        userver_resp_fields=[
            "HTTP/1.1 200 OK",
            "Cache-Control: private,no-store",
            "X-Response-ID: 11"
        ],
        userver_resp_body="Body for response 11",
        use_userver_host=False,
        userver_url="http://invalid.host/PARENT_PROXY",
        last_in_wave=True
    ),
    Txn(
        id="PARENT_PROXY_FAIL",
        req_mime_fields=[
            "X-Request-ID: 11"
        ],
        use_userver_host=False
    ),
    Txn(
        id="TRANSFORM2",
        req_mime_fields=[
            "X-Request-ID: 5",
            "Request:2"
        ],
        userver_resp_fields=[
            "HTTP/1.1 200 OK",
            "X-Response-ID: 5",
            "Cache-Control: max-age=86400",
            "Content-Type: text/html"
        ],
        userver_resp_body="Body for response 5"
    ),
    Txn(
        id="ALT_INFO2",
        duplicate_of="ALT_INFO1",
        req_mime_fields=[
            "X-Request-ID: 7",
            "Accept-Language: French"
        ]
    ),
    Txn(
        id="CACHE_DUP",
        duplicate_of="CACHE",
        last_in_wave=True
    ),
    Txn(
        id="TRANSFORM1_DUP",
        duplicate_of="TRANSFORM1"
    ),
    Txn(
        id="ALT_INFO3",
        duplicate_of="ALT_INFO1",
        req_mime_fields=[
            "X-Request-ID: 8",
            "Accept-Language: French, English"
        ],
        last_in_wave=True
    ),
    Txn(
        id="TRANSFORM2_DUP",
        duplicate_of="TRANSFORM2",
        last_in_wave=True
    )
]

port_list_str = ""
port_list = []
for txn in txns:
    # Reserve a dedicated ingress port on the proxy for each transaction.
    #
    Test.GetTcpPort(txn.id + "_port")
    txn.proxy_port = eval("Test.Variables." + txn.id + "_port")
    port_list_str += ",{}".format(txn.proxy_port)
    port_list += [txn.proxy_port]

server = Test.MakeOriginServer("server")

# This is an alternate server that accepts a connection, closes it without sending anything, and exits.
#
Test.GetTcpPort("Mute_server_port")
mute_server = Test.Processes.Process(
    "mute_server", "bash -c 'echo -n | nc -l {}'".format(Test.Variables.Mute_server_port)
)

# Generate a YAML file for the plugin.


class Empty:
    def __init__(self):
        0


plugin_data = Empty()
plugin_data.run_dir_path = Test.RunDirectory
plugin_data.server_port = server.Variables.Port
plugin_data.mute_server_port = Test.Variables.Mute_server_port
plugin_data.HOOKS_src_port = Test.Variables.HOOKS_src_port
plugin_data.txns = {}
plugin_data.proxy_port_to_txn = {}

for txn in txns:
    plugin_data.txns[txn.id] = {'proxy_port': txn.proxy_port}
    if txn.client_src_port:
        plugin_data.txns[txn.id]['src_port'] = txn.client_src_port

    # The test_tsapi2 plugin uses the number of the incoming port to determine the id of the transaction.
    #
    plugin_data.proxy_port_to_txn[txn.proxy_port] = txn.id

yaml_file_path = Test.RunDirectory + "/plugin.yaml"
yaml_file = open(yaml_file_path, "w")
yaml.dump(plugin_data, yaml_file)
yaml_file.close()

for txn in txns:
    if None is txn.duplicate_of:
        if txn.userver_resp_fields is not None:
            if txn.userver_url is not None:
                url = txn.userver_url
            else:
                url = "/" + txn.id
            request_header = {
                "headers": "GET " + url + " HTTP/1.0\r\nHost: doesnotmatter\r\n\r\n",
                "timestamp": "1469733493.993", "body": ""
            }
            h = ""
            for ln in txn.userver_resp_fields:
                h += ln + "\r\n"
            response_header = {"headers": h + "\r\n", "timestamp": "1469733493.993", "body": txn.userver_resp_body}
            server.addResponse("sessionlog.json", request_header, response_header)

# Generate a bash script file that performs all of the transactions in parallel in the background, waits for them
# to finish, and records each response in a separate file, and when all transactions are done, concatenates all
# the ressponse files into a single file.

cat_out_all = ""
script_file = open(Test.RunDirectory + "/txns.sh", "w")
script_file.write("PIDS=''\n\n")
for txn in txns:
    req_mime_fields = txn.req_mime_fields
    if txn.duplicate_of:
        txn_ = next(x for x in txns if x.id == txn.duplicate_of)
        if None is req_mime_fields:
            req_mime_fields = txn_.req_mime_fields
    else:
        txn_ = txn

    if txn_.use_userver_host:
        script_file.write(
            r"REQ='GET http://127.0.0.1:{}/{} HTTP/1.0\r\n'".format(server.Variables.Port, txn_.id) + "\n"
        )
    else:
        script_file.write(r"REQ='GET http://invalid.host/{} HTTP/1.0\r\n'".format(txn_.id) + "\n")

    for fld in req_mime_fields:
        script_file.write('REQ="${REQ}' + fld + r'\r\n"' + "\n")

    if txn.client_src_port:
        src_port_opt = "--source-port {}".format(txn.client_src_port)
    else:
        src_port_opt = ""

    script_file.write(
        r'printf "$REQ\r\n" | nc {} 127.0.0.1 {} > {}.out &'.format(src_port_opt, txn.proxy_port, txn.id) + "\n"
    )
    script_file.write('PIDS="$PIDS $!"\n\n')
    cat_out_all += "( cat " + txn.id + r".out ; printf '\n\n=====\n\n' )" + " >> all.out\n"

    if txn.last_in_wave:
        script_file.write(
            "wait $PIDS\n" +
            "PIDS=''\n\n"
        )

script_file.write(cat_out_all + "\n")
script_file.write(
    r"tr -d '\r' < all.out" + " | grep -v -e '^Age: ' -e '^Date: ' -e '^Server: ATS/' > all_filt.out\n\n"
)

script_file.write(
    "for F in *.tlog\n" +
    "do\n" +
    '  echo "=== $F ==="\n' +
    "  cat $F\n" +
    '  echo ""\n' +
    "done > all.log\n"
)
script_file.close()

ts = Test.MakeATSProcess("ts", select_ports=False)
ts.Ready = When.PortsOpen(port_list)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|dns',
    'proxy.config.http.server_ports': port_list_str[1:],
    'proxy.config.url_remap.remap_required': 1,
    'proxy.config.http.wait_for_cache': 1,
})

Test.PrepareTestPlugin(
    os.path.join(os.path.join(Test.TestDirectory, ".libs"), "test_tsapi2.so"), ts,
    plugin_args=(os.path.join(Test.RunDirectory, "plugin.yaml"))
)

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(mute_server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.Command = "bash -c '. txns.sh'"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
f = tr.Disk.File("all.log")
f.Content = "log.gold"
f = tr.Disk.File("all_filt.out")
f.Content = "out.gold"
