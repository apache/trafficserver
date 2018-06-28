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
**************

Synopsis
========
:program:`traffic_layout` SUBCOMMAND [OPTIONS]

Options
=============
.. program:: traffic_layout

.. option:: --run-root=[<path>]

   Use the run root file at :arg:`path`.

.. option:: -V, --version

    Print version information and exit.

Environment
===========

.. envvar:: TS_RUNROOT

   The path to the run root file. It has the same effect as the command line option :option:`--run-root`.

Description
=============
Document for the special functionality of ``runroot`` inside :program:`traffic_layout`. This feature
is for the setup of traffic server runroot. It will create a runtime sandbox for any program of
traffic server to run under.

Use :program:`traffic_layout` to create sandbox.
Run any program using the sandbox with ``--run-root=/path/to/file`` or ``--run-root``.

How it works
--------------

#. Create a sandbox directory for programs to run under.
#. Copy and symlink build time directories and files to the sandbox, allowing users to modify freely.
#. Emit a yaml file that defines layout structure for other programs to use (relative path).
#. Users are able to remove runroot and verify permission of the runroot.

Subcommands
=============

- Initialize the runroot: ::

      traffic_layout init (--path /path/to/sandbox/)

      Use the current working directory or the specific path to create runroot.
      The path can be relative or set up in :envvar:`TS_RUNROOT`.


- Remove the runroot: ::

      traffic_layout remove (--path /path/to/sandbox/)

      Find the sandbox to remove in following order: 

            1. specified in --path as absolute or relative.
            2. :envvar:`TS_RUNROOT`
            3. current working directory
            4. installed directory.

- Verify the runroot: ::

      traffic_layout verify (--path /path/to/sandbox/)

      Verify the permission of the sandbox.

Subcommands options
-------------------

- Force option: ::

      traffic_layout init --force (--path /path/to/sandbox)
      traffic_layout remove --force (--path /path/to/sandbox)

      Force init will create sandbox even if the directory is not empty.
      Force remove will remove a directory even if directory has no yaml file.

- Absolute option: ::

      traffic_layout init --absolute (--path /path/to/sandbox)

      create the sandbox and put directories in the yaml file in the form of absolute path.

- Fix option: ::

      traffic_layout verify --fix (--path /path/to/sandbox)

      Fix the permission issues verify found. ``--fix`` requires root privilege (sudo).

Usage for other programs:
==============================================

All programs can find the runroot to use in the following order

      #. specified in --run-root=/path/to/runroot (path needs to be absolute)
      #. :envvar:`TS_RUNROOT`
      #. current working directory
      #. installed directory

if none of the above is found as runroot, runroot will not be used
