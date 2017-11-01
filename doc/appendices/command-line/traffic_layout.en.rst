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

traffic_layout
*****************

.. program:: traffic_layout

.. option:: --run-root [<path>]

   Use the run root file at :arg:`path`.

Description
=============
Document for the special functionality of ``runroot`` inside :program:`traffic_layout` This feature
is for the setup of traffic server runroot. It will create a runtime sandbox for any program of
traffic server to run under.

#. Use :program:`traffic_layout` to create sandbox.
#. Run any program use the sandbox with ``--run-root=/path/to/file`` or ``--run-root``.

How it works:
--------------

#. Create a sandbox directory for programs to run under
#. Copy and symlink build time directories and files to sandbox, allowing users to modify freely.
#. Emit a yaml file that defines layout structure for other programs to use.

Options:
=============

#. Initialize the runroot: ::

      traffic_layout --init /path/to/sandbox/

   If no path is found, it will find :envvar:`TS_RUNROOT`.

#. Remove the runroot: ::

      traffic_layout --remove /path/to/sandbox/

   Remove the sandbox we created(check yaml file).
   If no path provided, it will find :envvar:`TS_RUNROOT`.
   If :envvar:`TS_RUNROOT` not found, it will find bin executing path & current working directory.

#. Force flag for creating: ::

      traffic_runroot --force --init /path/to/sandbox

   Force create sandbox and overwrite existing directory

Usage for other programs:
==============================================


Use pass in path or use :envvar:`TS_RUNROOT`.
If both not found, program will try to find bin path & current working directory. ::

   trafficserver --run-root=/path/to/runroot
   trafficserver --run-root

.. envvar:: TS_RUNROOT

   Path to run root file.

Notes
==========

.. note:: Path to sandbox must be an absolute path.
