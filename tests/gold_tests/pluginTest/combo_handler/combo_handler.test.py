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

Test.Summary = '''
Test combo_handler plugin
'''

# Skip if plugin not present.
#
Test.SkipUnless(
    Condition.PluginExists('combo_handler.so'),
)

# Function to generate a unique data file path (in the top level of the test's run directory),  put data (in string 'data') into
# the file, and return the file name.
#
_data_file__file_count = 0
def data_file(data):
    global _data_file__file_count
    file_path = Test.RunDirectory + "/tcp_client_in_{}".format(_data_file__file_count)
    _data_file__file_count += 1
    with open(file_path, "x") as f:
        f.write(data)
    return file_path

# Function to return command (string) to run tcp_client.py tool.  'host' 'port', and 'file_path' are the parameters to tcp_client.
#
def tcp_client_cmd(host, port, file_path):
    return "python3 {}/tcp_client.py {} {} {}".format(Test.Variables.AtsTestToolsDir, host, port, file_path)

# Function to return command (string) to run tcp_client.py tool.  'host' and 'port' are the first two parameters to tcp_client.
# 'data' is the data to put in the data file input to tcp_client.
#
def tcp_client(host, port, data):
    return tcp_client_cmd(host, port, data_file(data))

server = Test.MakeOriginServer("server")

def add_server_obj(content_type, path):
    request_header = {
        "headers": "GET " + path + " HTTP/1.1\r\n" +
            "Host: just.any.thing\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }
    response_header = {
        "headers": "HTTP/1.1 200 OK\r\n" +
            "Connection: close\r\n" +
            'Etag: "359670651"\r\n' +
            "Cache-Control: public, max-age=31536000\r\n" +
            "Accept-Ranges: bytes\r\n" +
            "Content-Type: " + content_type + "\r\n" +
            "\r\n",
        "timestamp": "1469733493.993",
        "body": "Content for " + path + "\n"
    }
    server.addResponse("sessionfile.log", request_header, response_header)

add_server_obj("text/css ; charset=utf-8", "/obj1")
add_server_obj("text/javascript", "/sub/obj2")
add_server_obj("text/argh", "/obj3")
add_server_obj("application/javascript", "/obj4")

ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|combo_handler',
})

ts.Disk.plugin_config.AddLine("combo_handler.so - - - ctwl.txt")

ts.Disk.remap_config.AddLine(
    'map http://xyz/ http://127.0.0.1/ @plugin=combo_handler.so'
)
ts.Disk.remap_config.AddLine(
    'map http://localhost/127.0.0.1/ http://127.0.0.1:{}/'.format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    'map http://localhost/sub/ http://127.0.0.1:{}/sub/'.format(server.Variables.Port)
)

ts.Disk.File(ts.Variables.CONFIGDIR + "/ctwl.txt", id="ctwl_cfg", typename="ats:config")
ts.Disk.ctwl_cfg.AddLine("# test  ")
ts.Disk.ctwl_cfg.AddLine("")
ts.Disk.ctwl_cfg.AddLine("		text/javascript  # test side comment")
ts.Disk.ctwl_cfg.AddLine("  application/javascript")
ts.Disk.ctwl_cfg.AddLine("text/css")

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(ts, ready=When.PortOpen(ts.Variables.port))
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.Command = "echo start stuff"
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = tcp_client("127.0.0.1", ts.Variables.port,
    "GET /admin/v1/combo?obj1&sub:obj2&obj3 HTTP/1.1\n" +
    "Host: xyz\n" +
    "Connection: close\n" +
    "\n"
)
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File("_output/1-tr-Default/stream.all.txt")
f.Content = "combo_handler_files/tr1.gold"

tr = Test.AddTestRun()
tr.Processes.Default.Command = tcp_client("127.0.0.1", ts.Variables.port,
    "GET /admin/v1/combo?obj1&sub:obj2&obj4 HTTP/1.1\n" +
    "Host: xyz\n" +
    "Connection: close\n" +
    "\n"
)
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File("_output/2-tr-Default/stream.all.txt")
f.Content = "combo_handler_files/tr2.gold"

ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR", "Some tests are failure tests")
