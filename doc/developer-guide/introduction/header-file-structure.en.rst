.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements. See the NOTICE file distributed with this work for additional information regarding
   copyright ownership. The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License. You may obtain a
   copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.

.. include:: ../../common.defs

.. _developer-header-files:

Header Files
************

The header files are located in the ``include/`` directory. There are several sub-directories, each containing header files for different purposes. Three of these are available to plugins.

"ts"
   The C Plugin API. These call functions directly embedded in ``traffic_server`` and therefore have
   no explicit linkage.

"tscpp/api"
   The C++ Plugin API. These call functions in the ``libtscppapi.so`` library to directly interact with
   the running |TS| instance.

.. note::

  The C++ Plugin API is deprecated in ATS 10.  It will be removed in ATS 11.

"tsutil"
   C++ utilities. These are standalone headers (although they may depend on other headers in the
   same directory). These provide functionality that is used inside the |TS| core logic but has been
   demonstrated to be useful for plugins as well [#]_. The functions are in the library
   ``libtsutil.so``, although many of the utilities are header only. This library is linked in to
   the ``traffic_server`` binary and so linkage may not be needed for a plugin.

   This library is independent of the C++ API and can be used with or without that library.

"tscore"
   |TS| core header files. These can only be used inside |TS| itself because they either depend on internal
   data structures either directly or operationally. This is linked in to the ``traffic_server`` binary therefore
   has no explicit linkage when used in the core.

New Plugin API layout
=====================

Previously, all plugin interfaces were built into the main |TS| binary. In an effort to enhance modularity and enable compile-time checks, these interfaces have been moved to ``src/api``. They are now isolated into a separate shared library ``tsapi.so``.

In addition, a new static library ``tsutil.a`` has been created, which contains code used by both the core and the plugins (via the plugin APIs), and is linked into the |TS| binary to keep functionalities consistent.

Note that ``tsapi.so`` depends on ``tsutil.a`` and other static libraries in
the core. ``tsapi.so`` is not statically linked against these dependencies
during its creation, but relies on them being linked into |TS|. To verify these
dependencies, a compile-time sanity check links ``tsapi.so`` with the main
Traffic Server binary (|TS|), ensuring all symbols required by ``tsapi.so`` will
be available in |TS|. The actual binding of these dependent symbols occurs
dynamically at runtime when |TS| is launched.

Historical
==========

This was a major restructuring the source code layout. The primary goal of this was to unify the include paths
for the core and plugins for these headers, and to make C++ support code used in the core also available to plugins.
Previously plugins in the |TS| codebase used these headers because they could reach over and include them, but
third party plugins could not because the headers were not installed.

"proxy/api/ts" was moved to "include/ts", along with a couple headers from "lib/ts". This consolidates the C plugin API.

The C++ API headers were split from the source and moved to "include/tscpp/api". The source files
were moved to "src/tscpp/api".

The contents of "lib/ts" were broken up and moved to different locations. The headers were moved to
"include/tscore" for core only headers, while headers to be made available to plugins were moved to
"include/tscpp/util". The corresponding source files were moved to "src/tscore" and "src/tscpp/util"
respectively. "libtsutil" was split in to "libtscore" for the core code and "libtscpputil" for shared
code.  Now "libtscpputil" has been combined with "tsapicore" headers into the "tsutil" library.

Appendix
========

.. rubric:: Footnotes

.. [#]
   Primarily by use in the plugins in the |TS| code base.
