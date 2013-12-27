Unable to Load Plugins
**********************

.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at
 
   http://www.apache.org/licenses/LICENSE-2.0
 
  Unless required by applicable law or agreed to in writing,
  software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  KIND, either express or implied.  See the License for the
  specific language governing permissions and limitations
  under the License.

To load plugins, follow the steps below.

1. Make sure that your plugin source code contains an ``TSPluginInit``
   initialization function.

2. Compile your plugin source code, creating a shared library.

3. Add an entry to the ``plugin.config`` file for your plugin.

4. Add the path to your plugin shared library to the :file:`records.config`
   file.

5. Restart Traffic Server.

For detailed information about each step above, refer to
:doc:`../getting-started/a-simple-plugin.en`.
