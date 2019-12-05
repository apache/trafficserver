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

.. include:: ../common.defs

.. _whats_new:

What's New in ATS v10.x
=======================

This version of |ATS| includes over <x> commits, from <y> pull requests. A total of <z> contributors
have participated in this development cycle.

.. toctree::
   :maxdepth: 1

New Features
------------
- Add support for dtrace style markers (SDT) and include a few markers at locations of interest to users of SystemTap, dtrace, and gdb. See :ref:`developer-debug-builds`.

``verify_global_plugin`` and ``verify_remap_plugin`` Maintenance Commands
    ``verify_global_plugin`` and ``verify_remap_plugin` are new maintenance
    commands added to |TS|. These load a plugin's shared object file and
    verify it meets minimal global or remap plugin API requirements.

New or modified Configurations
------------------------------

Logging and Metrics
-------------------

Plugins
-------
