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

.. include:: ../../common.defs

.. _developer-debug-builds:

Debug Builds
************

A debugger can set breakpoints in a plugin. Use a Traffic Server debug
build and compile the plugin with the ``-g`` option. A debugger can also
be used to analyze a core dump. To generate core, set the size limit of
the core files in the :file:`records.config` file to -1 as follows:

::

    CONFIG :ts:cv:`proxy.config.core_limit` INT -1

This is the equivalent of setting ``ulimit -c unlimited``

Debugging Tips:
~~~~~~~~~~~~~~~

-  Use a Traffic Server debug version.

-  Use assertions in your plugin (:c:func:`TSAssert` and :c:func:`TSReleaseAssert`).


