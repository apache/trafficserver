'''
Verify correct log retention behavior.
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

Test.Summary = '''
Test the enforcement of proxy.config.log.max_space_mb_for_logs.
'''

# This test is sensitive to timing issues, especially in the OS CI for some
# reason. We'll leave the test here because it is helpful for when doing
# development on the log rotate code, but make it generally skipped when the
# suite of AuTests are run so it doesn't generate annoying false negatives.
Test.SkipIf(Condition.true("This test is sensitive to timing issues which makes it flaky."))


class TestLogRetention:
    __base_records_config = {
        # Do not accept connections from clients until cache subsystem is operational.
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'logspace',

        # Enable log rotation and auto-deletion, the subjects of this test.
        'proxy.config.log.rolling_enabled': 3,
        'proxy.config.log.auto_delete_rolled_files': 1,

        # 10 MB is the minimum rolling size.
        'proxy.config.log.rolling_size_mb': 10,
        'proxy.config.log.periodic_tasks_interval': 1,
    }

    __server = None
    __ts_counter = 0
    __server_is_started = False

    def __init__(self, records_config, run_description, command="traffic_server"):
        """
        Create a TestLogRetention instance.
        """
        self.server = TestLogRetention.__create_server()
        self.ts = self.__create_ts(records_config, command)
        self.__initialize_processes()
        self.tr = Test.AddTestRun(run_description)

    def __initialize_processes(self):
        """
        Create a run to initialize the server and traffic_server processes so
        the caller doesn't have to.
        """
        tr = Test.AddTestRun("Initialize processes for ts{}".format(TestLogRetention.__ts_counter - 1))
        tr.Processes.Default.Command = self.get_curl_command()
        tr.Processes.Default.ReturnCode = 0
        if not TestLogRetention.__server_is_started:
            self.server.StartBefore(self.ts)
            tr.Processes.Default.StartBefore(self.server)
            TestLogRetention.__server_is_started = True
        else:
            tr.Processes.Default.StartBefore(self.ts)

        tr.StillRunningAfter = self.ts
        tr.StillRunningAfter = self.server

    @classmethod
    def __create_server(cls):
        """
        Create and return a server process.

        There is only one server process for all the tests. This function is
        re-entrant, but subsequent calls to it will return the cached version
        of the single server.
        """
        if cls.__server:
            return cls.__server

        server = Test.MakeOriginServer("server")
        request_header = {"headers": "GET / HTTP/1.1\r\n"
                          "Host: does.not.matter\r\n\r\n",
                          "timestamp": "1469733493.993", "body": ""}
        response_header = {"headers": "HTTP/1.1 200 OK\r\n"
                           "Connection: close\r\n"
                           "Cache-control: max-age=85000\r\n\r\n",
                           "timestamp": "1469733493.993", "body": "xxx"}
        server.addResponse("sessionlog.json", request_header, response_header)
        cls.__server = server
        return cls.__server

    def __create_ts(self, records_config, command="traffic_server"):
        """
        Create an ATS process.

        records_config: records_config values for this test.
        command: The ATS process to run for the test.
        """
        ts_name = "ts{counter}".format(counter=TestLogRetention.__ts_counter)
        TestLogRetention.__ts_counter += 1
        self.ts = Test.MakeATSProcess(ts_name, command=command, dump_runroot=True)

        combined_records_config = TestLogRetention.__base_records_config.copy()
        combined_records_config.update(records_config)
        self.ts.Disk.records_config.update(combined_records_config)

        self.ts.Disk.remap_config.AddLine(
            'map http://127.0.0.1:{0} http://127.0.0.1:{1}'.format(
                self.ts.Variables.port, self.server.Variables.Port)
        )
        return self.ts

    def get_curl_command(self):
        """
        Generate the appropriate single curl command.
        """
        return 'curl "http://127.0.0.1:{0}" --verbose'.format(
            self.ts.Variables.port)

    def get_command_to_rotate_once(self):
        """
        Generate the set of curl commands to trigger a log rotate.
        """
        return 'for i in {{1..2500}}; do curl "http://127.0.0.1:{0}" --verbose; done'.format(
            self.ts.Variables.port)

    def get_command_to_rotate_thrice(self):
        """
        Generate the set of curl commands to trigger a log rotate.
        """
        return 'for i in {{1..7500}}; do curl "http://127.0.0.1:{0}" --verbose; done'.format(
            self.ts.Variables.port)


#
# Test 0: Verify that log deletion happens when no min_count is specified.
#
specified_hostname = 'my_hostname'
twelve_meg_log_space = {
    # The following configures a 12 MB log cap with a required 2 MB head room.
    # Thus the rotated log of just over 10 MB should be deleted because it
    # will not leave enough head room.
    'proxy.config.log.max_space_mb_headroom': 2,
    'proxy.config.log.max_space_mb_for_logs': 12,
    # Verify that setting a hostname changes the hostname used in rolled logs.
    'proxy.config.log.hostname': specified_hostname,
}
test = TestLogRetention(twelve_meg_log_space,
                        "Verify log rotation and deletion of the configured log file with no min_count.")

# Configure approximately 5 KB entries for a log with no specified min_count.
test.ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: long
      format: "{prefix}: %<sssc>"
  logs:
    - filename: test_deletion
      format: long
'''.format(prefix="0123456789" * 500).split("\n")
)

# Verify that each log type was registered for auto-deletion.
test.ts.Streams.stderr = Testers.ContainsExpression(
    "Registering rotated log deletion for test_deletion.log with min roll count 0",
    "Verify test_deletion.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for error.log with min roll count 0",
    "Verify error.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for traffic.out with min roll count 0",
    "Verify traffic.out auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for diags.log with min roll count 0",
    "Verify diags.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for manager.log with min roll count 0",
    "Verify manager.log auto-delete configuration")
# Verify test_deletion was rotated and deleted.
test.ts.Streams.stderr += Testers.ContainsExpression(
    f"The rolled logfile.*test_deletion.log_{specified_hostname}.*was auto-deleted.*bytes were reclaimed",
    "Verify that space was reclaimed")

test.tr.Processes.Default.Command = test.get_command_to_rotate_once()
test.tr.Processes.Default.ReturnCode = 0

test.tr.StillRunningAfter = test.ts
test.tr.StillRunningAfter = test.server


#
# Test 1: Verify log deletion happens with a min_count of 1.
#
test = TestLogRetention(twelve_meg_log_space,
                        "Verify log rotation and deletion of the configured log file with a min_count of 1.")

# Configure approximately 5 KB entries for a log with no specified min_count.
test.ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: long
      format: "{prefix}: %<sssc>"
  logs:
    - filename: test_deletion
      rolling_min_count: 1
      format: long
'''.format(prefix="0123456789" * 500).split("\n")
)

# Verify that each log type was registered for auto-deletion.
test.ts.Streams.stderr = Testers.ContainsExpression(
    "Registering rotated log deletion for test_deletion.log with min roll count 1",
    "Verify test_deletion.log auto-delete configuration")
# Only the test_deletion should have its min_count overridden.
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for error.log with min roll count 0",
    "Verify error.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for traffic.out with min roll count 0",
    "Verify traffic.out auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for diags.log with min roll count 0",
    "Verify diags.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for manager.log with min roll count 0",
    "Verify manager.log auto-delete configuration")
# Verify test_deletion was rotated and deleted.
test.ts.Streams.stderr += Testers.ContainsExpression(
    f"The rolled logfile.*test_deletion.log_{specified_hostname}.*was auto-deleted.*bytes were reclaimed",
    "Verify that space was reclaimed")

test.tr.Processes.Default.Command = test.get_command_to_rotate_once()
test.tr.Processes.Default.ReturnCode = 0
test.tr.StillRunningAfter = test.ts
test.tr.StillRunningAfter = test.server


#
# Test 2: Verify log deletion happens for a plugin's logs.
#
test = TestLogRetention(twelve_meg_log_space,
                        "Verify log rotation and deletion of plugin logs.")
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'test_log_interface.so'), test.ts)

# Verify that the plugin's logs and other core logs were registered for deletion.
test.ts.Streams.stderr = Testers.ContainsExpression(
    "Registering rotated log deletion for test_log_interface.log with min roll count 0",
    "Verify test_log_interface.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for error.log with min roll count 0",
    "Verify error.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for traffic.out with min roll count 0",
    "Verify traffic.out auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for diags.log with min roll count 0",
    "Verify diags.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for manager.log with min roll count 0",
    "Verify manager.log auto-delete configuration")
# Verify test_deletion was rotated and deleted.
test.ts.Streams.stderr += Testers.ContainsExpression(
    "The rolled logfile.*test_log_interface.log_.*was auto-deleted.*bytes were reclaimed",
    "Verify that space was reclaimed")

test.tr.Processes.Default.Command = test.get_command_to_rotate_once()
test.tr.Processes.Default.ReturnCode = 0
test.tr.StillRunningAfter = test.ts
test.tr.StillRunningAfter = test.server

#
# Test 3: Verify log deletion priority behavior.
#
twenty_two_meg_log_space = {
    # The following configures a 22 MB log cap with a required 2 MB head room.
    # This should allow enough room for two logs being rotated.
    'proxy.config.log.max_space_mb_headroom': 2,
    'proxy.config.log.max_space_mb_for_logs': 22,
}
test = TestLogRetention(twenty_two_meg_log_space,
                        "Verify log deletion priority behavior.")

# Configure approximately 5 KB entries for a log with no specified min_count.
test.ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: long
      format: "{prefix}: %<sssc>"
  logs:
    - filename: test_low_priority_deletion
      rolling_min_count: 5
      format: long

    - filename: test_high_priority_deletion
      rolling_min_count: 1
      format: long
'''.format(prefix="0123456789" * 500).split("\n")
)

# Verify that each log type was registered for auto-deletion.
test.ts.Streams.stderr = Testers.ContainsExpression(
    "Registering rotated log deletion for test_low_priority_deletion.log with min roll count 5",
    "Verify test_low_priority_deletion.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for test_high_priority_deletion.log with min roll count 1",
    "Verify test_high_priority_deletion.log auto-delete configuration")
# Only the test_deletion should have its min_count overridden.
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for error.log with min roll count 0",
    "Verify error.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for traffic.out with min roll count 0",
    "Verify traffic.out auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for diags.log with min roll count 0",
    "Verify diags.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for manager.log with min roll count 0",
    "Verify manager.log auto-delete configuration")
# Verify test_deletion was rotated and deleted.
test.ts.Streams.stderr += Testers.ExcludesExpression(
    "The rolled logfile.*test_low_priority_deletion.log_.*was auto-deleted.*bytes were reclaimed",
    "Verify that space was reclaimed from test_high_priority_deletion")

# Verify that ATS derives the hostname correctly if the user does not specify a
# hostname via 'proxy.config.log.hostname'.
hostname = socket.gethostname()
test.ts.Streams.stderr += Testers.ContainsExpression(
    f"The rolled logfile.*test_high_priority_deletion.log_{hostname}.*was auto-deleted.*bytes were reclaimed",
    "Verify that space was reclaimed from test_high_priority_deletion")

test.tr.Processes.Default.Command = test.get_command_to_rotate_once()
test.tr.Processes.Default.ReturnCode = 0
test.tr.StillRunningAfter = test.ts
test.tr.StillRunningAfter = test.server

#
# Test 4: Verify min_count configuration overrides.
#
various_min_count_overrides = {
    'proxy.config.log.max_space_mb_for_logs': 22,
    'proxy.config.log.rolling_min_count': 3,
    'proxy.config.output.logfile.rolling_min_count': 4,
    'proxy.config.diags.logfile.rolling_min_count': 5,
}
test = TestLogRetention(various_min_count_overrides,
                        "Verify that the various min_count configurations behave as expected")

# Only the test_deletion should have its min_count overridden.
test.ts.Streams.stderr = Testers.ContainsExpression(
    "Registering rotated log deletion for error.log with min roll count 3",
    "Verify error.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for traffic.out with min roll count 4",
    "Verify traffic.out auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for diags.log with min roll count 5",
    "Verify diags.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for manager.log with min roll count 5",
    "Verify manager.log auto-delete configuration")
# In case a future log is added, make sure the developer doesn't forget to
# set the min count per configuration.
test.ts.Streams.stderr += Testers.ExcludesExpression(
    "Registering .* with min roll count 0",
    "Verify nothing has a default min roll count of 0 per configuration")

# This test doesn't require a log rotation. We just verify that the logs communicate
# the appropriate min_count values above.
test.tr.Processes.Default.Command = test.get_curl_command()
test.tr.Processes.Default.ReturnCode = 0
test.tr.StillRunningAfter = test.ts
test.tr.StillRunningAfter = test.server


#
# Test 5: Verify log deletion does not happen when it is disabled.
#
auto_delete_disabled = twelve_meg_log_space.copy()
auto_delete_disabled.update({
    'proxy.config.log.auto_delete_rolled_files': 0,
    # Verify that setting a hostname changes the hostname used in rolled logs.
    'proxy.config.log.hostname': 'my_hostname',
})
test = TestLogRetention(auto_delete_disabled,
                        "Verify log deletion does not happen when auto-delet is disabled.")

# Configure approximately 5 KB entries for a log with no specified min_count.
test.ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: long
      format: "{prefix}: %<sssc>"
  logs:
    - filename: test_deletion
      rolling_min_count: 1
      format: long
'''.format(prefix="0123456789" * 500).split("\n")
)

# Verify that each log type was registered for auto-deletion.
test.ts.Streams.stderr = Testers.ExcludesExpression(
    "Registering rotated log deletion for test_deletion.log with min roll count 1",
    "Verify test_deletion.log auto-delete configuration")
# Only the test_deletion should have its min_count overridden.
test.ts.Streams.stderr += Testers.ExcludesExpression(
    "Registering rotated log deletion for error.log with min roll count 0",
    "Verify error.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ExcludesExpression(
    "Registering rotated log deletion for traffic.out with min roll count 0",
    "Verify traffic.out auto-delete configuration")
test.ts.Streams.stderr += Testers.ExcludesExpression(
    "Registering rotated log deletion for diags.log with min roll count 0",
    "Verify diags.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ExcludesExpression(
    "Registering rotated log deletion for manager.log with min roll count 0",
    "Verify manager.log auto-delete configuration")
# Verify test_deletion was not deleted.
test.ts.Streams.stderr += Testers.ExcludesExpression(
    "The rolled logfile.*test_deletion.log_.*was auto-deleted.*bytes were reclaimed",
    "Verify that space was reclaimed")

test.tr.Processes.Default.Command = test.get_command_to_rotate_once()
test.tr.Processes.Default.ReturnCode = 0
test.tr.StillRunningAfter = test.ts
test.tr.StillRunningAfter = test.server

#
# Test 6: Verify that max_roll_count is respected.
#
max_roll_count_of_2 = {
    'proxy.config.diags.debug.tags': 'log-file',

    # Provide plenty of max_space: we want auto-deletion to happen because of
    # rolling_max_count, not max_space_mb_for_logs.
    'proxy.config.log.max_space_mb_headroom': 2,
    'proxy.config.log.max_space_mb_for_logs': 100,

    # This is the configuration under test.
    'proxy.config.log.rolling_max_count': 2,
}
test = TestLogRetention(max_roll_count_of_2,
                        "Verify max_roll_count is respected.")

# Configure approximately 5 KB entries for a log with no specified min_count.
test.ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: long
      format: "{prefix}: %<sssc>"
  logs:
    - filename: test_deletion
      format: long
'''.format(prefix="0123456789" * 500).split("\n")
)

# Verify that trim happened for the rolled file.
test.ts.Streams.stderr = Testers.ContainsExpression(
    "rolled logfile.*test_deletion.log.*old.* was auto-deleted",
    "Verify test_deletion.log was trimmed")

test.tr.Processes.Default.Command = test.get_command_to_rotate_thrice()
test.tr.Processes.Default.ReturnCode = 0
test.tr.StillRunningAfter = test.ts
test.tr.StillRunningAfter = test.server

#
# Test 7: Verify log deletion happens after a config reload.
#
test = TestLogRetention(twelve_meg_log_space,
                        "Verify log rotation and deletion after a config reload.")

test.ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: long
      format: "{prefix}: %<sssc>"
  logs:
    - filename: test_deletion
      format: long
'''.format(prefix="0123456789" * 500).split("\n")
)

# Verify that the plugin's logs and other core logs were registered for deletion.
test.ts.Streams.stderr = Testers.ContainsExpression(
    "Registering rotated log deletion for test_deletion.log with min roll count 0",
    "Verify test_deletion.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for error.log with min roll count 0",
    "Verify error.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for traffic.out with min roll count 0",
    "Verify traffic.out auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for diags.log with min roll count 0",
    "Verify diags.log auto-delete configuration")
test.ts.Streams.stderr += Testers.ContainsExpression(
    "Registering rotated log deletion for manager.log with min roll count 0",
    "Verify manager.log auto-delete configuration")
# Verify test_deletion was rotated and deleted.
test.ts.Streams.stderr += Testers.ContainsExpression(
    "The rolled logfile.*test_deletion.log_.*was auto-deleted.*bytes were reclaimed",
    "Verify that space was reclaimed")

# Touch logging.yaml so the config reload applies to logging objects.
test.tr.Processes.Default.Command = "touch " + test.ts.Disk.logging_yaml.AbsRunTimePath
test.tr.Processes.Default.ReturnCode = 0
test.tr.StillRunningAfter = test.ts
test.tr.StillRunningAfter = test.server

# Set TS_RUNROOT, traffic_ctl needs it to find the socket.
test.ts.SetRunRootEnv()

tr = Test.AddTestRun("Perform a config reload")
tr.Processes.Default.Command = "traffic_ctl config reload"
tr.Processes.Default.Env = test.ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5
tr.StillRunningAfter = test.ts
tr.StillRunningAfter = test.server

tr = Test.AddTestRun("Get the log to rotate.")
tr.Processes.Default.Command = test.get_command_to_rotate_once()
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = test.ts
tr.StillRunningAfter = test.server
