'''Verify the listening UDS socket mode honors uds-perm and defaults to 0666.'''
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
Verify proxy.config.http.server_ports honors the uds-perm option on UDS
listeners and defaults to 0666.
'''


def assert_socket_mode(socket_path: str, expected_octal: str) -> str:
    return (
        f'python3 -c "import os, stat, sys; '
        f'sys.exit(0 if stat.S_IMODE(os.stat(sys.argv[1]).st_mode) == int(sys.argv[2], 8) else 1)" '
        f'{socket_path} {expected_octal}')


#
# Default UDS permission should be 0666 (no uds-perm option specified).
#
ts_default = Test.MakeATSProcess("ts_default")
ts_default.Disk.records_config.update(
    {
        'proxy.config.http.server_ports': f"{ts_default.Variables.port} {ts_default.Variables.uds_path}",
    })

tr = Test.AddTestRun("UDS default permission is 0666")
tr.Processes.Default.Command = assert_socket_mode(ts_default.Variables.uds_path, '0666')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts_default)

#
# uds-perm=0660 should be honored.
#
ts_custom = Test.MakeATSProcess("ts_custom")
ts_custom.Disk.records_config.update(
    {
        'proxy.config.http.server_ports': f"{ts_custom.Variables.port} {ts_custom.Variables.uds_path}:uds-perm=0660",
    })

tr = Test.AddTestRun("UDS custom permission 0660 honored")
tr.Processes.Default.Command = assert_socket_mode(ts_custom.Variables.uds_path, '0660')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts_custom)
