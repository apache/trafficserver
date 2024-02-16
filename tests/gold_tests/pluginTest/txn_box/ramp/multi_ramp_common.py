# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
class State:
    TxnCount = 6
    # transactions per repeat.
    LogCount = RepeatCount * TxnCount  # total number of log lines expected.
    Target = "stage.video.ex"  # indicates the request was redirected / ramped
    Description = "Checking count."

    # The paths in the replay file to check for ramping.
    ramps = {
        "v1/video/channels": [10, 0],
        "v1/video/search": [30, 0],
        "v1/video/alias": [30, 0],
        "v1/audio": [0, 0],
        "v2/video": [0, 0],
        "v1/video/sub": [100, 0]
    }

    def validate(self, log_path):
        result = ""

        try:
            with open(log_path, mode='r') as log:
                lines = log.readlines()
                for l in lines:
                    if self.Target in l:
                        for r in self.ramps.items():
                            if r[0] in l:
                                r[1][1] += 1
        except:
            pass

        for r in self.ramps.items():
            target = r[1][0]
            if target == 0:
                lower = upper = 0
            elif target == 100:
                lower = upper = RepeatCount
            else:
                lower = int((RepeatCount * (r[1][0] - 5)) / 100)
                upper = int((RepeatCount * (r[1][0] + 5)) / 100)

            if r[1][1] < lower or r[1][1] > upper:
                result = "{}'{}' failed with {} not in {}..{}\n".format(result, r[0], r[1][1], lower, upper)

        if len(result) == 0:
            return (True, self.Description, "OK")
        return (False, self.Description, result)

    def log_check(self, log_path):
        try:
            with open(log_path, mode='r') as log:
                lines = log.readlines()
                if len(lines) >= self.LogCount:
                    return True
        except:
            pass
        return False


def ramp_test_fixup(tr):
    state = State()
    tr.Variables.State = state

    ts = tr.Variables.TS
    ts.Setup.Copy("multi-ramp.replay.yaml", ts.Variables.CONFIGDIR)
    ts.Setup.Copy("ramp.logging.yaml", os.path.join(ts.Variables.CONFIGDIR, "logging.yaml"))
    ts.Setup.Copy("../ssl/server.key", ts.Variables.SSLDir)
    ts.Setup.Copy("../ssl/server.pem", ts.Variables.SSLDir)

    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'txn_box',
            'proxy.config.http.cache.http': 0,
            'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir)
            # enable ssl port
            ,
            'proxy.config.http.server_ports': '{0} {1}:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
            'proxy.config.ssl.client.verify.server.policy': 'DISABLED',
            'proxy.config.ssl.server.cipher_suite':
                'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2'
        })
    ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

    pv_client = tr.Variables.CLIENT

    # Final process to force ready checks for previous ones.
    trailer = tr.Processes.Process("trailer")
    trailer.Command = "sh -c :"

    # Log watcher to wait until the log is finalized.
    watcher = tr.Processes.Process("log-watch")
    watcher.Command = "sleep 1000"
    watcher.StartupTimeout = 120
    # This doesn't work either
    #watcher.Ready = lambda : LogCheck(os.path.join(ts.Variables.LOGDIR, "ramp.log" ))
    # ready flag doesn't work here.
    #watcher.StartBefore(pv_client, ready=lambda : LogCheck(os.path.join(ts.Variables.LOGDIR, "ramp.log" )))
    # Only this works
    log_path = os.path.join(ts.Variables.LOGDIR, "ramp.log")
    pv_client.StartAfter(watcher, ready=lambda: state.log_check(log_path))
    # ready flag doesn't work here.
    watcher.StartAfter(trailer)
    watcher.Streams.All.Content = Testers.Lambda(lambda info, tester: state.validate(log_path))
