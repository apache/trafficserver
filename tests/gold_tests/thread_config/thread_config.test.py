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

import sys

Test.Summary = 'Test that Trafficserver starts with different thread configurations.'
Test.ContinueOnFail = True

ts = Test.MakeATSProcess('ts-1_exec-0_accept-1_task-1_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 1,
    'proxy.config.accept_threads': 0,
    'proxy.config.task_threads': 1,
    'proxy.config.cache.threads_per_disk': 1,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})
ts.Setup.CopyAs('check_threads.py', Test.RunDirectory)

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 1 -a 0 -t 1 -c 1'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

ts = Test.MakeATSProcess('ts-1_exec-1_accept-2_task-8_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 1,
    'proxy.config.accept_threads': 1,
    'proxy.config.task_threads': 2,
    'proxy.config.cache.threads_per_disk': 8,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 1 -a 1 -t 2 -c 8'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

ts = Test.MakeATSProcess('ts-1_exec-10_accept-10_task-32_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 1,
    'proxy.config.accept_threads': 10,
    'proxy.config.task_threads': 10,
    'proxy.config.cache.threads_per_disk': 32,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 1 -a 10 -t 10 -c 32'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

ts = Test.MakeATSProcess('ts-2_exec-0_accept-1_task-1_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 2,
    'proxy.config.accept_threads': 0,
    'proxy.config.task_threads': 1,
    'proxy.config.cache.threads_per_disk': 1,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 2 -a 0 -t 1 -c 1'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

ts = Test.MakeATSProcess('ts-2_exec-1_accept-2_task-8_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 2,
    'proxy.config.accept_threads': 1,
    'proxy.config.task_threads': 2,
    'proxy.config.cache.threads_per_disk': 8,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 2 -a 1 -t 2 -c 8'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

ts = Test.MakeATSProcess('ts-2_exec-10_accept-10_task-32_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 2,
    'proxy.config.accept_threads': 10,
    'proxy.config.task_threads': 10,
    'proxy.config.cache.threads_per_disk': 32,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 2 -a 10 -t 10 -c 32'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

ts = Test.MakeATSProcess('ts-32_exec-0_accept-1_task-1_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 32,
    'proxy.config.accept_threads': 0,
    'proxy.config.task_threads': 1,
    'proxy.config.cache.threads_per_disk': 1,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 32 -a 0 -t 1 -c 1'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

ts = Test.MakeATSProcess('ts-32_exec-1_accept-2_task-8_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 32,
    'proxy.config.accept_threads': 1,
    'proxy.config.task_threads': 2,
    'proxy.config.cache.threads_per_disk': 8,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 32 -a 1 -t 2 -c 8'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

ts = Test.MakeATSProcess('ts-32_exec-10_accept-10_task-32_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 32,
    'proxy.config.accept_threads': 10,
    'proxy.config.task_threads': 10,
    'proxy.config.cache.threads_per_disk': 32,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 32 -a 10 -t 10 -c 32'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

ts = Test.MakeATSProcess('ts-100_exec-0_accept-1_task-1_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 100,
    'proxy.config.accept_threads': 0,
    'proxy.config.task_threads': 1,
    'proxy.config.cache.threads_per_disk': 1,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 100 -a 0 -t 1 -c 1'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

ts = Test.MakeATSProcess('ts-100_exec-1_accept-2_task-8_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 100,
    'proxy.config.accept_threads': 1,
    'proxy.config.task_threads': 2,
    'proxy.config.cache.threads_per_disk': 8,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 100 -a 1 -t 2 -c 8'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

ts = Test.MakeATSProcess('ts-100_exec-10_accept-10_task-32_aio')
ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 100,
    'proxy.config.accept_threads': 10,
    'proxy.config.task_threads': 10,
    'proxy.config.cache.threads_per_disk': 32,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'iocore_thread_start|iocore_net_accept_start'})

tr = Test.AddTestRun()
TS_ROOT = ts.Env['TS_ROOT']
tr.Processes.Default.Command = f'{sys.executable} check_threads.py -p {TS_ROOT} -e 100 -a 10 -t 10 -c 32'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
