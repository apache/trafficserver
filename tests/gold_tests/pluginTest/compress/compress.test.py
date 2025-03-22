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
Test compress plugin
'''

# This test case is very bare-bones.  It only covers a few scenarios that have caused problems.

# Skip if plugins not present.
#
Test.SkipUnless(
    Condition.PluginExists('compress.so'), Condition.PluginExists('conf_remap.so'), Condition.HasATSFeature('TS_HAS_BROTLI'))

server = Test.MakeOriginServer("server", options={'--load': f'{Test.TestDirectory}/compress_observer.py'})

# Need a fairly big body, otherwise the plugin will refuse to compress
line = "lets go surfin now everybodys learnin how"
body = f'{line}\n' * 24 + line
orig_path = f'{Test.RunDirectory}/orig.txt'
open(orig_path, 'w').write(body)

# expected response from the origin server
response_header = {
    "headers":
        "HTTP/1.1 200 OK\r\nConnection: close\r\n" + 'Etag: "359670651"\r\n' + "Cache-Control: public, max-age=31536000\r\n" +
        "Accept-Ranges: bytes\r\n" + "Content-Type: text/javascript\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": body
}
for i in range(3):
    # add request/response to the server dictionary
    request_header = {"headers": f"GET /obj{i} HTTP/1.1\r\nHost: just.any.thing\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
    server.addResponse("sessionfile.log", request_header, response_header)

# post for the origin server
post_request_header = {
    "headers":
        "POST /obj3 HTTP/1.1\r\nHost: just.any.thing\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 11\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "knock knock"
}
server.addResponse("sessionfile.log", post_request_header, response_header)


def curl(ts, idx, encodingList, out_path):
    return (
        f"-o {out_path} --verbose --proxy http://127.0.0.1:{ts.Variables.port}"
        f" --header 'X-Ats-Compress-Test: {idx}/{encodingList}'"
        f" --header 'Accept-Encoding: {encodingList}' 'http://ae-{idx}/obj{idx}'"
        " 2>> compress_long.log ; printf '\n===\n' >> compress_long.log")


def curl_post(ts, idx, encodingList, out_path):
    return (
        f"-o {out_path} --verbose -d 'knock knock' --proxy http://127.0.0.1:{ts.Variables.port}"
        f" --header 'X-Ats-Compress-Test: {idx}/{encodingList}'"
        f" --header 'Accept-Encoding: {encodingList}' 'http://ae-{idx}/obj{idx}'"
        " 2>> compress_long.log ; printf '\n===\n' >> compress_long.log")


waitForServer = True

waitForTs = True

ts = Test.MakeATSProcess("ts", enable_cache=False)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'compress',
        'proxy.config.http.normalize_ae': 0,
    })

ts.Setup.Copy("compress.config")
ts.Setup.Copy("compress2.config")

ts.Disk.remap_config.AddLine(
    f'map http://ae-0/ http://127.0.0.1:{server.Variables.Port}/' +
    f' @plugin=compress.so @pparam={Test.RunDirectory}/compress.config')
ts.Disk.remap_config.AddLine(
    f'map http://ae-1/ http://127.0.0.1:{server.Variables.Port}/' +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=1' +
    f' @plugin=compress.so @pparam={Test.RunDirectory}/compress.config')
ts.Disk.remap_config.AddLine(
    f'map http://ae-2/ http://127.0.0.1:{server.Variables.Port}/' +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.normalize_ae=2' +
    f' @plugin=compress.so @pparam={Test.RunDirectory}/compress2.config')
ts.Disk.remap_config.AddLine(
    f'map http://ae-3/ http://127.0.0.1:{server.Variables.Port}/' +
    f' @plugin=compress.so @pparam={Test.RunDirectory}/compress.config')

out_path_counter = 0


def get_out_path():
    global out_path_counter
    out_path = f'{Test.RunDirectory}/curl_out_{out_path_counter}'
    out_path_counter += 1
    return out_path


deflate_path = f'{Test.RunDirectory}/deflate.txt'


def get_verify_command(out_path, decrompressor):
    return f"{decrompressor} -c {out_path} > {deflate_path} && diff {deflate_path} {orig_path}"


for i in range(3):

    tr = Test.AddTestRun(f'gzip, deflate, sdch, br: {i}')
    if (waitForTs):
        tr.Processes.Default.StartBefore(ts)
    waitForTs = False
    if (waitForServer):
        tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
    waitForServer = False
    tr.Processes.Default.ReturnCode = 0
    out_path = get_out_path()
    tr.CurlCommand(curl(ts, i, 'gzip, deflate, sdch, br', out_path))
    tr = Test.AddTestRun(f'verify gzip, deflate, sdch, br: {i}')
    tr.ReturnCode = 0
    if i == 0:
        tr.Processes.Default.Command = get_verify_command(out_path, "brotli -d")
    elif i == 1:
        tr.Processes.Default.Command = get_verify_command(out_path, "gunzip -k")
    elif i == 2:
        tr.Processes.Default.Command = get_verify_command(out_path, "brotli -d")

    tr = Test.AddTestRun(f'gzip: {i}')
    tr.Processes.Default.ReturnCode = 0
    out_path = get_out_path()
    tr.CurlCommand(curl(ts, i, "gzip", out_path))
    tr = Test.AddTestRun(f'verify gzip: {i}')
    tr.ReturnCode = 0
    tr.Processes.Default.Command = get_verify_command(out_path, "gunzip -k")

    tr = Test.AddTestRun(f'br: {i}')
    tr.Processes.Default.ReturnCode = 0
    out_path = get_out_path()
    tr.CurlCommand(curl(ts, i, "br", out_path))
    tr = Test.AddTestRun(f'verify br: {i}')
    tr.ReturnCode = 0
    if i == 1:
        tr.Processes.Default.Command = f"diff {out_path} {orig_path}"
    else:
        tr.Processes.Default.Command = get_verify_command(out_path, "brotli -d")

    tr = Test.AddTestRun(f'deflate: {i}')
    tr.Processes.Default.ReturnCode = 0
    out_path = get_out_path()
    tr.CurlCommand(curl(ts, i, "deflate", out_path))
    tr = Test.AddTestRun(f'verify deflate: {i}')
    tr.ReturnCode = 0
    tr.Processes.Default.Command = f"diff {out_path} {orig_path}"

# Test Accept-Encoding normalization.

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
out_path = get_out_path()
tr.CurlCommand(curl(ts, 0, "gzip;q=0.666", out_path))
tr = Test.AddTestRun(f'verify gzip;q=0.666')
tr.ReturnCode = 0
tr.Processes.Default.Command = get_verify_command(out_path, "gunzip -k")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
out_path = get_out_path()
tr.CurlCommand(curl(ts, 0, "gzip;q=0.666x", out_path))
tr = Test.AddTestRun(f'verify gzip;q=0.666x')
tr.ReturnCode = 0
tr.Processes.Default.Command = get_verify_command(out_path, "gunzip -k")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
out_path = get_out_path()
tr.CurlCommand(curl(ts, 0, "gzip;q=#0.666", out_path))
tr = Test.AddTestRun(f'verify gzip;q=#0.666')
tr.ReturnCode = 0
tr.Processes.Default.Command = get_verify_command(out_path, "gunzip -k")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
out_path = get_out_path()
tr.CurlCommand(curl(ts, 0, "gzip; Q = 0.666", out_path))
tr = Test.AddTestRun(f'verify gzip; Q = 0.666')
tr.ReturnCode = 0
tr.Processes.Default.Command = get_verify_command(out_path, "gunzip -k")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
out_path = get_out_path()
tr.CurlCommand(curl(ts, 0, "gzip;q=0.0", out_path))
tr = Test.AddTestRun(f'verify gzip;q=0.0')
tr.ReturnCode = 0
tr.Processes.Default.Command = f"diff {out_path} {orig_path}"

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
out_path = get_out_path()
tr.CurlCommand(curl(ts, 0, "gzip;q=-0.1", out_path))
tr = Test.AddTestRun(f'verify gzip;q=-0.1')
tr.ReturnCode = 0
tr.Processes.Default.Command = get_verify_command(out_path, "gunzip -k")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
out_path = get_out_path()
tr.CurlCommand(curl(ts, 0, "aaa, gzip;q=0.666, bbb", out_path))
tr = Test.AddTestRun(f'verify aaa, gzip;q=0.666, bbb')
tr.ReturnCode = 0
tr.Processes.Default.Command = get_verify_command(out_path, "gunzip -k")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
out_path = get_out_path()
tr.CurlCommand(curl(ts, 0, " br ; q=0.666, bbb", out_path))
tr = Test.AddTestRun(f'verify br ; q=0.666, bbb')
tr.ReturnCode = 0
tr.Processes.Default.Command = get_verify_command(out_path, "brotli -d")

tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
out_path = get_out_path()
tr.CurlCommand(curl(ts, 0, "aaa, gzip;q=0.666 , ", out_path))
tr = Test.AddTestRun(f'verify aaa, gzip;q=0.666 , ')
tr.ReturnCode = 0
tr.Processes.Default.Command = get_verify_command(out_path, "gunzip -k")

# post
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
out_path = get_out_path()
tr.CurlCommand(curl_post(ts, 3, "gzip", out_path))
tr = Test.AddTestRun(f'verify gzip post')
tr.ReturnCode = 0
tr.Processes.Default.Command = get_verify_command(out_path, "gunzip -k")

# compress_long.log contains all the output from the curl commands.  The tr removes the carriage returns for easier
# readability.  Curl seems to have a bug, where it will neglect to output an end of line before outputting an HTTP
# message header line.  The sed command is a work-around for this problem.  greplog.sh uses the grep command to
# select HTTP request/response line that should be consistent every time the test runs.
#
tr = Test.AddTestRun()
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Command = (
    r"tr -d '\r' < compress_long.log | sed 's/\(..*\)\([<>]\)/\1\n\2/' | {0}/greplog.sh > compress_short.log").format(
        Test.TestDirectory)
f = tr.Disk.File("compress_short.log")
f.Content = "compress.gold"

tr = Test.AddTestRun()
tr.Processes.Default.Command = "echo"
f = tr.Disk.File("compress_userver.log")
f.Content = "compress_userver.gold"
