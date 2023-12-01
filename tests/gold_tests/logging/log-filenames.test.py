'''
Verify log file naming behavior.
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
import ports

Test.Summary = '''
Verify log file naming behavior.
'''


class LogFilenamesTest:
    """ Common test configuration logic across the filename tests.
    """

    # A counter for the ATS process to make each of them unique.
    __ts_counter = 1

    # The default log names for the various system logs.
    default_log_data = {'diags': 'diags.log', 'error': 'error.log', 'manager': 'manager.log'}

    def __init__(self, description, log_data=default_log_data):
        ''' Handle initialization tasks common across the tests.

        Args:
            description (str): The description of the test. This is passed to
            the TestRun.

            log_data (dict): The log name information passed to the
            MakeATSProcess extension.
        '''
        self.__description = description
        self.ts = self.__configure_traffic_manager(log_data)
        self.tr = self.__configure_traffic_TestRun(description)
        self.__configure_await_TestRun(self.sentinel_log_path)

    def __configure_traffic_manager(self, log_data):
        ''' Common ATS configuration logic.

        Args:
            log_data (dict): The log name information passed to the
            MakeATSProcess extension.

        Return:
            The traffic_manager process.
        '''
        self._ts_name = f"ts{LogFilenamesTest.__ts_counter}"
        LogFilenamesTest.__ts_counter += 1
        self.ts = Test.MakeATSProcess(self._ts_name, command="traffic_manager", use_traffic_out=False, log_data=log_data)
        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 0,
                'proxy.config.diags.debug.tags': 'log',
                'proxy.config.log.periodic_tasks_interval': 1,
            })

        # Intentionally retrieve a port that is closed, that is no server is
        # listening on it. We will use this to attempt talking with a
        # non-existent server, which will result in an error log entry.
        ports.get_port(self.ts, 'closed_port')
        self.ts.Disk.remap_config.AddLines(
            [
                f'map /server/down http://127.0.0.1:{self.ts.Variables.closed_port}',
                'map / https://trafficserver.apache.org @action=deny',
            ])

        # The following log is configured so that we can wait upon it being
        # written so we know that ATS is done writing logs.
        self.sentinel_log_filename = "sentinel"
        self.ts.Disk.logging_yaml.AddLine(
            f'''
            logging:
              formats:
                - name: url_and_return_code
                  format: "%<cqu>: %<pssc>"
              logs:
                - filename: {self.sentinel_log_filename}
                  format: url_and_return_code
            ''')

        self.sentinel_log_path = os.path.join(self.ts.Variables.LOGDIR, f"{self.sentinel_log_filename}.log")

        return self.ts

    def __configure_await_TestRun(self, log_path):
        ''' Configure a TestRun that awaits upon the provided log_path to
        exist.

        Args:
            log_path (str): The log file upon which we will wait.
        '''
        description = self.__description
        tr = Test.AddTestRun(f'Awaiting log files to be written for: {description}')
        condwait_path = os.path.join(Test.Variables.AtsTestToolsDir, 'condwait')
        tr.Processes.Default.Command = f'{condwait_path} 60 1 -f {log_path}'
        tr.Processes.Default.ReturnCode = 0

    def __configure_traffic_TestRun(self, description):
        ''' Configure a TestRun to run the expected transactions.

        Args:
            description (str): The description to use for the TestRun.
        '''
        tr = Test.AddTestRun(f'Run traffic for: {description}')
        tr.Processes.Default.Command = (
            f'curl http://127.0.0.1:{self.ts.Variables.port}/some/path --verbose --next '
            f'http://127.0.0.1:{self.ts.Variables.port}/server/down --verbose')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.ts)

    def configure_named_custom_log(self, custom_log_filename):
        """ Configure ATS to log to the custom log file via logging.yaml.

        Args:
            custom_log_filename (str): The name of the custom log file to
            configure.

        Return:
            The path to the configured custom log file.
        """
        self.custom_log_filename = custom_log_filename
        self.ts.Disk.logging_yaml.AddLine(
            f'''
                - filename: {custom_log_filename}
                  format: url_and_return_code
            ''')

        if custom_log_filename in ('stdout', 'stderr'):
            self.custom_log_path = custom_log_filename
            if custom_log_filename == 'stdout':
                self.ts.Disk.custom_log = self.ts.Streams.stdout
            else:
                self.ts.Disk.custom_log = self.ts.Streams.stderr
        else:
            self.custom_log_path = os.path.join(self.ts.Variables.LOGDIR, f"{custom_log_filename}.log")
            self.ts.Disk.File(self.custom_log_path, id="custom_log")
        return self.custom_log_path

    def set_log_expectations(self):
        ''' Configure sanity checks for each of the log types (manager, error,
        etc.) to verify they are emitting the expected content.
        '''
        manager_path = self.ts.Disk.manager_log.AbsPath
        self.ts.Disk.manager_log.Content += Testers.ContainsExpression(
            "Launching ts process", f"{manager_path} should contain traffic_manager log messages")

        diags_path = self.ts.Disk.diags_log.AbsPath
        self.ts.Disk.diags_log.Content += Testers.ContainsExpression(
            "Traffic Server is fully initialized", f"{diags_path} should contain traffic_server diag messages")

        error_log_path = self.ts.Disk.error_log.AbsPath
        self.ts.Disk.error_log.Content += Testers.ContainsExpression(
            "CONNECT: attempt fail", f"{error_log_path} should contain connection error messages")

        custom_log_path = self.ts.Disk.custom_log.AbsPath
        self.ts.Disk.custom_log.Content += Testers.ContainsExpression(
            "https://trafficserver.apache.org/some/path: 403", f"{custom_log_path} should contain the custom transaction logs")


class DefaultNamedTest(LogFilenamesTest):
    ''' Verify that if custom names are not configured, then the default
    'diags.log', 'manager.log', and 'error.log' are written to.
    '''

    def __init__(self):
        super().__init__('default log filename configuration')

        # For these tests, more important than the listening port is the
        # existence of the log files. In particular, it can take a few seconds
        # for traffic_manager to open diags.log.
        self.diags_log = self.ts.Disk.diags_log.AbsPath
        self.ts.Ready = When.FileExists(self.diags_log)

        self.configure_named_custom_log('my_custom_log')
        self.set_log_expectations()


class CustomNamedTest(LogFilenamesTest):
    ''' Verify that the user can assign custom filenames to manager.log, etc.
    '''

    def __init__(self):
        log_data = {'diags': 'my_diags.log', 'error': 'my_error.log', 'manager': 'my_manager.log'}
        super().__init__('specify log filename configuration', log_data)

        # Configure custom names for manager.log, etc.
        self.ts.Disk.records_config.update(
            {
                'proxy.node.config.manager_log_filename': 'my_manager.log',
                'proxy.config.diags.logfile.filename': 'my_diags.log',
                'proxy.config.error.logfile.filename': 'my_error.log',
            })

        # For these tests, more important than the listening port is the
        # existence of the log files. In particular, it can take a few seconds
        # for traffic_manager to open diags.log.
        self.diags_log = self.ts.Disk.diags_log.AbsPath
        self.ts.Ready = When.FileExists(self.diags_log)

        self.configure_named_custom_log('my_custom_log')
        self.set_log_expectations()


class stdoutTest(LogFilenamesTest):
    ''' Verify that we can configure the logs to go to stdout.
    '''

    def __init__(self):

        log_data = {'diags': 'stdout', 'error': 'stdout', 'manager': 'stdout'}
        super().__init__('specify logs to go to stdout', log_data)

        # Configure custom names for manager.log, etc.
        self.ts.Disk.records_config.update(
            {
                'proxy.node.config.manager_log_filename': 'stdout',
                'proxy.config.diags.logfile.filename': 'stdout',
                'proxy.config.error.logfile.filename': 'stdout',
            })

        self.configure_named_custom_log('stdout')

        # The diags.log file will not be created since we are piping to stdout.
        # Therefore, simply wait upon the port being open.
        self.ts.Ready = When.PortOpen(self.ts.Variables.port)
        self.set_log_expectations()


class stderrTest(LogFilenamesTest):
    '''
    Verify that we can configure the logs to go to stderr.
    '''

    def __init__(self):

        log_data = {'diags': 'stderr', 'error': 'stderr', 'manager': 'stderr'}
        super().__init__('specify logs to go to stderr', log_data)

        # Configure custom names for manager.log, etc.
        self.ts.Disk.records_config.update(
            {
                'proxy.node.config.manager_log_filename': 'stderr',
                'proxy.config.diags.logfile.filename': 'stderr',
                'proxy.config.error.logfile.filename': 'stderr',
            })

        self.configure_named_custom_log('stderr')

        # The diags.log file will not be created since we are piping to stderr.
        # Therefore, simply wait upon the port being open.
        self.ts.Ready = When.PortOpen(self.ts.Variables.port)
        self.set_log_expectations()


#
# Run the tests.
#
DefaultNamedTest()
CustomNamedTest()
stdoutTest()

# The following stderr test can be run successfully by hand using the replay
# files from the sandbox. All the expected output goes to stderr. However, for
# some reason during the AuTest run, the stderr output stops emitting after the
# logging.yaml file is parsed. This is left here for now because it is valuable
# for use during development, but it is left commented out so that it doesn't
# produce the false failure in CI and developer test runs.
# stderrTest()
