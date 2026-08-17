'''
Test __shutdown__ lua global plugin hook.
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
import sys

Test.Summary = '''
Test __shutdown__ lua global plugin hook
'''

Test.SkipUnless(Condition.PluginExists('tslua.so'),)

Test.ContinueOnFail = True

# Helper script for signaling a traffic_server process by command-line identifier
# match. Reused from gold_tests/logging.
TS_PID_SCRIPT = 'ts_process_handler.py'

server = Test.MakeOriginServer("server")

# The identifier shutdown_race_client.py matches on to find this process.
ts = Test.MakeATSProcess("lua_shutdown_ts")

Test.Setup.Copy("global_shutdown.lua")
Test.Setup.Copy("remap_shutdown.lua")
Test.Setup.Copy("shutdown_race_client.py")
Test.Setup.Copy(os.path.join(Test.TestDirectory, '..', '..', 'logging', TS_PID_SCRIPT))

# Where global_shutdown.lua numbers the Lua states and reports which one is busy.
ts.Env['TS_LUA_SHUTDOWN_TEST_DIR'] = Test.RunDirectory

request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

# A remap instance of the plugin as well as the global one. A global script's
# __shutdown__ releases process global resources, so the remap Lua states have to
# be held across it too, and stay held: a remap callback queued on one of those
# mutexes would otherwise enter Lua as soon as they were released.
# A single remap Lua state, so that requests queue on its mutex rather than
# spreading across states.
ts.Disk.remap_config.AddLine(
    'map http://remap.example.com/ http://127.0.0.1:{}/'
    ' @plugin=tslua.so @pparam=--states=1 @pparam={}/remap_shutdown.lua'.format(server.Variables.Port, Test.RunDirectory))
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{}/'.format(server.Variables.Port))

# Use 2 states so the shutdown handler is called a predictable number of times.
ts.Disk.plugin_config.AddLine('tslua.so --states=2 {}/global_shutdown.lua'.format(Test.RunDirectory))

ts.Disk.records_config.update(
    {
        # With 2 states and 4 event threads, each state is used by 2 event threads:
        # concurrent /hold requests then keep state 1 busy no matter which event
        # thread the shutdown continuation is dispatched to.
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 4,
        # Shut down as soon as SIGTERM is received, while the load is still running.
        'proxy.config.stop.shutdown_timeout': 0,
        # Load the remap instance of the plugin from the same image as the global
        # instance, so that the shutdown handler sees the remap Lua states too. With
        # dynamic reload enabled the remap instance is a private copy of the .so with
        # Lua states of its own.
        'proxy.config.plugin.dynamic_reload_mode': 0,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ts_lua',
    })

curl_and_args = '-s -D /dev/stdout -o /dev/stderr -x localhost:{} '.format(ts.Variables.port)

# 0 Test - Send a request to confirm the global plugin is active.
tr = Test.AddTestRun("Lua global read request hook fires for HTTP requests")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(ts)
tr.MakeCurlCommand(curl_and_args + 'http://www.example.com/', ts=ts)
ps.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Verify do_global_read_request was invoked for the HTTP request above.
ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    r'do_global_read_request called', 'do_global_read_request should be called for HTTP requests')

# 1 Test - Exercise the remap instance of the plugin.
tr = Test.AddTestRun("Lua remap instance handles requests")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + 'http://remap.example.com/remap-hello', ts=ts)
ps.ReturnCode = 0
ps.Streams.stderr = Testers.ContainsExpression('Remap Lua response', 'the remap Lua script should generate the response')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 2 Test - SIGTERM ATS while a Lua state is executing a request callback.
tr = Test.AddTestRun("Shut down while a Lua state is running Lua code")
ps = tr.Processes.Default
ps.Command = (
    f'{sys.executable} ./shutdown_race_client.py '
    f'127.0.0.1 {ts.Variables.port} {Test.RunDirectory} lua_shutdown_ts && sleep 3')
ps.ReturnCode = 0
tr.StillRunningAfter = server

# 3 Test - Traffic Server finished shutting down. The shutdown handler holds every
# Lua state mutex, so a deadlock there would leave the process alive.
tr = Test.AddTestRun("Traffic Server exited")
ps = tr.Processes.Default
# A non-zero return code means no matching traffic_server process was found.
ps.Command = f'{sys.executable} ./{TS_PID_SCRIPT} lua_shutdown_ts'
ps.ReturnCode = 1
tr.StillRunningAfter = server

# The shutdown handler calls __shutdown__ once per Lua state (2 states
# configured), and only after Lua execution in every state has quiesced.
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r'shutdown barrier acquired', 'the shutdown handler should exclude every Lua state before calling __shutdown__')
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r'__shutdown__ called for state 0', '__shutdown__ should be called for Lua state 0')
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r'__shutdown__ called for state 1', '__shutdown__ should be called for Lua state 1')
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    r'__shutdown__ overlapped an active Lua state', '__shutdown__ should not overlap a request callback in another Lua state')

# ProxyMutex is recursive per event thread, so the event thread that dispatches
# the shutdown hook can re-enter a Lua state it locked itself. The load runs past
# the hook, which catches that.
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    r'do_global_read_request ran after __shutdown__', 'no Lua callback should run once __shutdown__ has been called')

# The remap states are released to no one: a remap callback queued behind the
# barrier must not enter Lua after the global __shutdown__ freed what it uses.
ts.Disk.traffic_out.Content += Testers.ContainsExpression(r'do_remap called', 'the remap Lua script should run before shutdown')
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    r'do_remap ran after __shutdown__', 'no remap Lua callback should run once __shutdown__ has been called')

ts.Disk.diags_log.Content = Testers.ExcludesExpression(
    r'skipping __shutdown__', 'the Lua states should go idle well inside the shutdown barrier timeout')

# A second Traffic Server, shut down with nothing in flight, which is how a
# drained host restarts. The shutdown barrier keeps every Lua state mutex, so
# anything ATS does later in shutdown that wants one of them would hang here. No
# TS_LUA_SHUTDOWN_TEST_DIR here: the scripts then just run plainly.
quiet_ts = Test.MakeATSProcess("lua_shutdown_quiet_ts")
quiet_ts.Disk.remap_config.AddLine(
    'map http://remap.example.com/ http://127.0.0.1:{}/'
    ' @plugin=tslua.so @pparam=--states=1 @pparam={}/remap_shutdown.lua'.format(server.Variables.Port, Test.RunDirectory))
quiet_ts.Disk.plugin_config.AddLine('tslua.so --states=2 {}/global_shutdown.lua'.format(Test.RunDirectory))
quiet_ts.Disk.records_config.update(
    {
        'proxy.config.stop.shutdown_timeout': 0,
        'proxy.config.plugin.dynamic_reload_mode': 0,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ts_lua',
    })

# 4 Test - Use the remap instance so that it has Lua states to tear down.
tr = Test.AddTestRun("Quiet Traffic Server serves a remap Lua request")
ps = tr.Processes.Default
ps.StartBefore(quiet_ts)
tr.MakeCurlCommand(
    '-s -D /dev/stdout -o /dev/stderr -x localhost:{} http://remap.example.com/remap-hello'.format(quiet_ts.Variables.port),
    ts=quiet_ts)
ps.ReturnCode = 0
ps.Streams.stderr = Testers.ContainsExpression('Remap Lua response', 'the remap Lua script should generate the response')
tr.StillRunningAfter = quiet_ts
tr.StillRunningAfter = server

# 5 Test - Shut it down with nothing in flight.
tr = Test.AddTestRun("Shut down the quiet Traffic Server")
ps = tr.Processes.Default
ps.Command = f'{sys.executable} ./{TS_PID_SCRIPT} lua_shutdown_quiet_ts --signal TERM && sleep 3'
ps.ReturnCode = 0
tr.StillRunningAfter = server

# 6 Test - It has to have exited: remap teardown must not block on the barrier.
tr = Test.AddTestRun("Quiet Traffic Server exited")
ps = tr.Processes.Default
# A non-zero return code means no matching traffic_server process was found.
ps.Command = f'{sys.executable} ./{TS_PID_SCRIPT} lua_shutdown_quiet_ts'
ps.ReturnCode = 1

quiet_ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    r'shutdown barrier acquired', 'the shutdown handler should take the barrier on a quiet shutdown too')
quiet_ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r'__shutdown__ called for state 0', '__shutdown__ should be called on a quiet shutdown')
