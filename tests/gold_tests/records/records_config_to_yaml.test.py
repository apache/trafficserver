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
import os
import yaml

Test.Summary = 'Testing ATS records to yaml script.'

file_suffix = 1

# case 1 TLS with no data
tr = Test.AddTestRun("Test records to yaml convert script - full file")

tr.Setup.Copy(os.path.join(Test.Variables.RepoDir, "tools/records/convert2yaml.py"))
tr.Setup.Copy('legacy_config/full_records.config')
tr.Processes.Default.Command = f'python3 convert2yaml.py -f full_records.config --output generated{file_suffix}.yaml --yaml --mute'
f = tr.Disk.File(f"generated{file_suffix}.yaml")
f.Content = "gold/full_records.yaml"

file_suffix = file_suffix + 1

tr = Test.AddTestRun("Test records to yaml convert script -only renamed records.")
tr.Setup.Copy(os.path.join(Test.Variables.RepoDir, "tools/records/convert2yaml.py"))
tr.Setup.Copy('legacy_config/old_records.config')
tr.Processes.Default.Command = f'python3 convert2yaml.py -f old_records.config --output generated{file_suffix}.yaml --yaml'
tr.Processes.Default.Stream = 'gold/renamed_records.gold'
f = tr.Disk.File(f"generated{file_suffix}.yaml")
f.Content = "gold/renamed_records.yaml"

tr = Test.AddTestRun("Test errors when trying to override values ")
tr.Setup.Copy(os.path.join(Test.Variables.RepoDir, "tools/records/convert2yaml.py"))
tr.Setup.Copy('legacy_config/override_value.config')
tr.Processes.Default.Command = f'python3 convert2yaml.py -f override_value.config --output generated{file_suffix}.yaml --yaml -m'
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "We cannot continue with 'proxy.config.ssl.client.verify.server.policy' at line '3' as a value node will be overridden",
    "Error should be present")

tr = Test.AddTestRun("Test errors when trying to override maps")
tr.Setup.Copy(os.path.join(Test.Variables.RepoDir, "tools/records/convert2yaml.py"))
tr.Setup.Copy('legacy_config/override_map.config')
tr.Processes.Default.Command = f'python3 convert2yaml.py -f override_map.config --output generated{file_suffix}.yaml --yaml -m'
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "We cannot continue with 'proxy.config.ssl.client.verify.server' at line '3' as an existing YAML map will be overridden.",
    "Error should be present")
