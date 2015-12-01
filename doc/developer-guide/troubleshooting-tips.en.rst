Troubleshooting Tips
********************

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

This appendix lists the following troubleshooting tips.

.. toctree::
   :maxdepth: 2

   troubleshooting-tips/unable-to-load-plugins.en
   troubleshooting-tips/unable-to-debug-tags.en
   troubleshooting-tips/using-a-debugger.en
   troubleshooting-tips/debugging-memory-leaks.en


Unable to Compile Plugins
-------------------------

The process for compiling a shared library varies with the platform
used, so the Traffic Server API includes the :program:`tsxs` script you can use
to create shared libraries on all supported Traffic Server platforms.

Example
~~~~~~~

Assuming the sample program is stored in the file ``hello-world.c``, you
could use the following commands to building a shared library:

::

        tsxs -c hello-world.c -o hello-world.so

To install this plugin in your ``plugindir`` use the equivalent of sudo
on your platform to execute:

::

        sudo tsxs -o hello-world.so -i

