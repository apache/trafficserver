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

========
Synopsis
========

:program:`traffic_layout` SUBCOMMAND [OPTIONS]

===========
Environment
===========

.. envvar:: TS_RUNROOT

   The path to the run root file. It has the same effect as the command line option :option:`--run-root`.

===========
Description
===========

Document for the special functionality of ``runroot`` inside :program:`traffic_layout`. This feature
is for the setup of traffic server runroot. It will create a runtime sandbox for any program of
traffic server to run under.

=====
Usage
=====

First we need to create a runroot. It can be created simply by calling command `init`. ::

    traffic_layout init --path /path/to/runroot

A runroot will be created in ``/path/to/runroot``, available for other programs to use.
If the path is not specified, the current working directory will be used.

To run traffic_manager, for example, using the runroot, there are several ways:
    #. ``/path/to/runroot/bin/traffic_manager``
    #. ``traffic_manager --run-root=/path/to/runroot``
    #. ``traffic_manager --run-root=/path/to/runroot/runroot.yaml``
    #. Set :envvar:`TS_RUNROOT` to ``/path/to/runroot`` and run ``traffic_manager``
    #. Run ``traffic_manager`` with ``/path/to/runroot`` as current working directory

.. Note::

   if none of the above is found as runroot, runroot will not be used and the program will fall back to the default.

===========
Subcommands
===========

init
----
Use the current working directory or the specific path to create runroot.
The path can be absolute or relative.

workflow:
    #. Create a sandbox directory for programs to run under.
    #. Copy and symlink build time directories and files to the sandbox, allowing users to modify freely.
    #. Emit a YAML file that defines layout structure for other programs to use (relative path).

Example: ::

    traffic_layout init (--path /path/to/sandbox/) (--force) (--absolute) (--copy-style=[HARD/SOFT/FULL]) (--layout=special_layout.yml)

For the ``--layout=[<YAML file>]`` option, a custom layout can be used to construct a runroot.
Below is an example of customized yaml file (custom.yml) to construct. ::

    prefix: ./runroot
    exec_prefix: ./runroot
    bindir: ./runroot/custom_bin
    sbindir: ./runroot/custom_sbin
    sysconfdir: ./runroot/custom_sysconf
    datadir: ./runroot/custom_data
    includedir: ./runroot/custom_include
    libdir: ./runroot/custom_lib
    libexecdir: ./runroot/custom_libexec
    localstatedir: ./runroot/custom_localstate
    runtimedir: ./runroot/custom_runtime
    logdir: ./runroot/custom_log
    cachedir: ./runroot/custom_cache

If ``traffic_layout init --layout="custom.yml"`` is executed, a runroot following the format above will be created.

.. Note::

   :file:`storage.config` does not use the cachedir value, but makes its relatives paths relative to the base prefix.
   So please update the directory for cache in ``storage.config`` according to the customized runroot.

remove
------
Find the sandbox to remove in following order:
    #. specified in --path as absolute or relative.
    #. current working directory.
    #. installed directory.

Example: ::

    traffic_layout remove (--path /path/to/sandbox/) (--force)

verify
------
Verify the permission of the sandbox. The permission issues can be fixed with ``--fix`` option.
``--with-user`` option can be used to verify the permission of the runroot for specific user.

Example: ::

    traffic_layout verify (--path /path/to/sandbox/) (--fix) (--with-user root)

=======
Options
=======
.. program:: traffic_layout

.. option:: --run-root=[<path>]

    Use the run root file at :arg:`path`.

.. option:: -V, --version

    Print version information and exit.

.. option:: -h, --help

    Print usage information and exit.

.. option:: -p, --path

    Specify the path of runroot for commands (init, remove, verify).

init
----
.. option:: -f, --force

    Force init will create sandbox even if the directory is not empty.

.. option:: -a, --absolute

    Put directories in the YAML file in the form of absolute paths when creating.

.. option:: -c, --copy-style [HARD/SOFT/FULL]

    Specify the way of copying executables when creating runroot.
    HARD means hard link. SOFT means symlink. FULL means full copy.

.. option:: -l, --layout [<YAML file>]

    Use specific layout (providing YAML file) to create runroot.

remove
------
.. option:: `-f`, `--force`

    Force remove will remove the directory even if it has no YAML file.

verify
------

.. option:: -x, --fix

    Fix the permission issues verify found. ``--fix`` requires root privilege (sudo).

.. option:: -w, --with-user

    Verify runroot with certain user. The value can be passed in as the username or ``#`` followed by uid.
