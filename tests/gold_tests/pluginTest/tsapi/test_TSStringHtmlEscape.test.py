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
#  Unless required by applicable law or agreed to in writing,
#  software distributed under the License is distributed on an
#  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#  KIND, either express or implied.  See the License for the
#  specific language governing permissions and limitations
#  under the License.

import os

Test.Summary = "Verify the HTML escape and unescape APIs with a response-transform plugin."

plugin_name = "test_TSStringHtmlEscape"
test_run = Test.ATSReplayTest(replay_file="test_TSStringHtmlEscape.replay.yaml")
ts = test_run.Processes.ts

plugin_path = os.path.join(
    Test.Variables.AtsBuildGoldTestsDir,
    "pluginTest",
    "tsapi",
    ".libs",
    f"{plugin_name}.so",
)
ts.Setup.Copy(plugin_path, ts.Env["PROXY_CONFIG_PLUGIN_PLUGIN_DIR"])
