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


SystemTap and DTrace support
****************************

Traffic Server can be instrumented with **SystemTap** on Linux systems, or
**DTrace** on \*BSDs and macOS. In order to use such tools, Traffic Server needs
to be built with ``-g``, or the debug symbols need to be installed. On Debian
systems, install the ``trafficserver-dbgsym`` package to install the debug
symbols.

In addition to the normal probe points that can be used with SystemTap and
DTrace, such as function calls and specific statements, Traffic Server does
provide SDT markers at various interesting code paths.

Pass the ``--enable-systemtap`` flag to ``./configure`` in order to build
Traffic Server with dtrace style markers (SDT). On Traffic Server builds with
SDT markers enabled, you can list the available markers with ``stap -L
'process("/path/to/traffic_server").mark("*")``.

See the `SystemTap documentation <https://sourceware.org/systemtap/wiki/AddingUserSpaceProbingToApps>`_ and the `DTrace guide <http://dtrace.org/guide/chp-sdt.html>`_ for more information.
