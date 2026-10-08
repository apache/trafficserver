'''
Verify configuring the jax_fingerprint plugin with command-line options.

Each plugin.config line or remap @plugin instance configures a single
fingerprint with the original command-line options instead of a YAML
configuration file.
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

Test.Summary = __doc__
Test.SkipUnless(Condition.PluginExists('jax_fingerprint.so'))
exec(open(os.path.join(Test.TestDirectory, 'jax_fingerprint_common.py')).read())

# The same setups configured with command-line options instead of a file.
JaxFingerprintTest('Global JA4H overwrite command line', 'JA4H', 'global', cli=True)
JaxFingerprintTest('Remap JA3 standalone command line', 'JA3', 'remap', cli=True)
JaxFingerprintTest('Hybrid JA4 servernames command line', 'JA4', 'hybrid', servernames='jax.server.test', cli=True)
JaxFingerprintTest('Global JA4H log-field command line', 'JA4H', 'global', log_field='jaxja4hcli', cli=True)

# The JA3, JA4, and JA4H methods, each loaded by its own plugin.config line.
AllMethodsTest('Multiple methods loaded by command-line options', cli=True)
