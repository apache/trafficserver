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
#. Run any program using the sandbox with ``--run-root=/path/to/file`` or ``--run-root``.

How it works:
--------------

#. Create a sandbox directory for programs to run under.
#. Copy and symlink build time directories and files to the sandbox, allowing users to modify freely.
#. Emit a yaml file that defines layout structure for other programs to use (relative path).

Options:
=============

#. Initialize the runroot: ::

      traffic_layout init (--path /path/to/sandbox/)

   Use the current working directory or specific path to create runroot.

#. Remove the runroot: ::

      traffic_layout remove --path /path/to/sandbox/

   Remove the sandbox we created(check yaml file).
   If no path provided, it will check bin executing path and current working directory to clean.

#. Use Force flag for creating: ::

      traffic_layout init --force (--path /path/to/sandbox)
      traffic_layout remove --force --path /path/to/sandbox

   Force create sandbox and overwrite existing directory when directory is not empty or has a yaml file in it.
   Force removing a directory when directory has no yaml file.

#. Use absolute flag for creating: ::

      traffic_layout init --absolute (--path /path/to/sandbox)

   create sandbox and put directories in the yaml file with absolute path form.

Usage for other programs:
==============================================

Use command line path or use :envvar:`TS_RUNROOT`.
If command line path and envvar are not found, program will try to find the current executing program bin path or current working directory to use as runroot if the yaml file is found. 
For bin path and cwd, it can go one level up to the parent directory to find the yaml file. ::

   trafficserver --run-root=/path/to/runroot (use /path/to/runroot as runroot)
   trafficserver --run-root                  (use $TS_RUNROOT as runroot)

.. envvar:: TS_RUNROOT

   The path to run root directory.

Notes
==========

.. note:: Path to sandbox must be an absolute path.
