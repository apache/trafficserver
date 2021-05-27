'''
Specific test for number_of_redirections config.
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
Test.Summary = '''
Test redirection/location & number of redirects(number_of_redirections config)
'''


class NumberOfRedirectionsTest:
    '''
        Handy class to test with number_of_redirections values. Three servers will be created and request
        will flow between them. Depending on the configured number_of_redirections, some request may be
        followed by ATS and some directly by the client(curl)
    '''

    def __init__(self, testName, numberOfRedirections):
        self._numberOfRedirections = numberOfRedirections
        self._tr = Test.AddTestRun(testName)
        self.setup_ts(f'ts{self._numberOfRedirections}')
        self.setup_dns()
        self.setup_verifier_servers()
        self.add_config()

    def setup_ts(self, name="ts"):
        self._ts = Test.MakeATSProcess(name, enable_cache=False)

    def setup_verifier_servers(self):
        self._srv3 = Test.MakeVerifierServerProcess(f"srv3_{self._numberOfRedirections}", "replay/redirect_srv3_replay.yaml")
        self._srv2 = Test.MakeVerifierServerProcess(
            f"srv2_{self._numberOfRedirections}",
            "replay/redirect_srv2_replay.yaml",
            context={
                "vs_http_port": self._srv3.Variables.http_port})
        self._srv1 = Test.MakeVerifierServerProcess(
            f"srv1_{self._numberOfRedirections}",
            "replay/redirect_srv1_replay.yaml",
            context={
                "vs_http_port": self._srv2.Variables.http_port})

    def setup_dns(self):
        self._dns = Test.MakeDNServer(f"dns_{self._numberOfRedirections}")
        self._dns.addRecords(records={"a.test": ["127.0.0.1"]})
        self._dns.addRecords(records={"b.test": ["127.0.0.1"]})
        self._dns.addRecords(records={"c.test": ["127.0.0.1"]})

    def add_config(self):
        self._ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http|dns|redirect|http_redirect',
            'proxy.config.http.number_of_redirections': self._numberOfRedirections,
            'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
            'proxy.config.dns.resolv_conf': 'NULL',
            'proxy.config.url_remap.remap_required': 0,  # need this so the domain gets a chance to be evaluated through DNS
            'proxy.config.http.redirect.actions': 'self:follow',  # redirects to self are not followed by default
        })
        self._ts.Disk.remap_config.AddLines([
            'map a.test/ping http://a.test:{0}/'.format(self._srv1.Variables.http_port),
            'map b.test/pong http://b.test:{0}/'.format(self._srv2.Variables.http_port),
            'map c.test/pang http://c.test:{0}/'.format(self._srv3.Variables.http_port),
        ])

    def run(self):
        self._tr.Processes.Default.StartBefore(self._srv1)
        self._tr.Processes.Default.StartBefore(self._srv2)
        self._tr.Processes.Default.StartBefore(self._srv3)
        self._tr.Processes.Default.StartBefore(self._dns)
        self._tr.Processes.Default.StartBefore(self._ts)
        self._tr.Command = "curl -L -vvv a.test/ping --proxy 127.0.0.1:{0} -H 'uuid: redirect_test_1'".format(
            self._ts.Variables.port)
        self._tr.Processes.Default.Streams.All = f"gold/number_of_redirections_{self._numberOfRedirections}.gold"
        self._tr.ReturnCode = 0
        self._tr.StillRunningAfter = self._ts


# No redirect, curl will get 302 and do the rest of the redirection.
NumberOfRedirectionsTest("Test number_of_redirections=0", 0).run()
# Single redirect, ATS will follow 1 redirect and the client the last one.
NumberOfRedirectionsTest("Test number_of_redirections=1", 1).run()
# The client will just get 200OK and no redirect will be done by it, TS will follow the two
# 302 Redirect.
NumberOfRedirectionsTest("Test number_of_redirections=2", 2).run()
# If adding more, need to touch the server side  as well. It wont be enough.
