'''
Verify the legacy storage.config + volume.config fallback path.
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
from enum import Enum

Test.Summary = '''
Verify Apache Traffic Server falls back to legacy storage.config and
volume.config when storage.yaml is absent.
'''


class LegacyStorageConfigFallbackTest:
    """
    When storage.yaml is missing, ATS should load the legacy
    storage.config + volume.config files instead.
    """

    class State(Enum):
        """
        State of process
        """
        INIT = 0
        RUNNING = 1

    def __init__(self):
        self.state = self.State.INIT
        self.__setupTS()

    def __setupTS(self):
        self.ts = Test.MakeATSProcess("ts", use_legacy_storage=True)

        self.ts.Setup.CopyAs(
            os.path.join(Test.TestDirectory, 'legacy_storage_config', 'storage.config'), self.ts.Variables.CONFIGDIR)
        self.ts.Setup.CopyAs(
            os.path.join(Test.TestDirectory, 'legacy_storage_config', 'volume.config'), self.ts.Variables.CONFIGDIR)

        self.ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'cache_init',
        })

        self.ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'storage\.yaml not found, falling back to storage\.config \+ volume\.config',
            'storage.yaml fallback Note should be logged when only legacy files are present')

        self.ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'storage\.config & volume\.config finished loading',
            'Legacy storage.config + volume.config should be reported as finished loading')

    def __checkProcessBefore(self, tr):
        if self.state == self.State.RUNNING:
            tr.StillRunningBefore = self.ts
        else:
            tr.Processes.Default.StartBefore(self.ts)
            self.state = self.State.RUNNING

    def __checkProcessAfter(self, tr):
        assert (self.state == self.State.RUNNING)
        tr.StillRunningAfter = self.ts

    def __testFallback(self):
        """
        ATS starts cleanly using legacy storage.config + volume.config
        when storage.yaml is absent.
        """
        tr = Test.AddTestRun("Verify ATS started cleanly with legacy storage.config + volume.config")
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Env = self.ts.Env
        tr.Processes.Default.Command = 'traffic_ctl --debug metric get proxy.process.cache.bytes_total'
        tr.Processes.Default.ReturnCode = 0
        self.__checkProcessAfter(tr)

    def __testConfigRegistry(self):
        """
        traffic_ctl config registry should list the legacy storage.config and
        volume.config entries that were registered as static files.
        """
        tr = Test.AddTestRun("Verify config registry lists legacy storage.config and volume.config")
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Env = self.ts.Env
        tr.Processes.Default.Command = 'traffic_ctl config registry'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            r'storage\.config', 'storage.config should appear in the file registry')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            r'volume\.config', 'volume.config should appear in the file registry')
        self.__checkProcessAfter(tr)

    def run(self):
        self.__testFallback()
        self.__testConfigRegistry()


LegacyStorageConfigFallbackTest().run()
