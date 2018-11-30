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

.. _runroot:

Runroot
*******

Preface
=======

Runroot is a powerful feature to detect and define layout at runtime. This document helps to guide through how runroot works.
For management and setup of runroots, please refer to ``appendices/command-line/traffic_layout.en``

Why do we need runroot
======================

Runroot is a replacing approach for the previous ``TS_ROOT`` logic. ``TS_ROOT`` is based on replacing a compile-time package install root location.
Layouts for many systems have data location that are absolute paths that are defined without ``$PREFIX`` variable. So, the current logic is difficult to follow
and not consistent between trafficserver programs. Furthermore, it is not easy to modify subdirectory locations.

So, we have the runroot which makes ATS easier to use, develop and deploy.

Main logic
==========

Everything in runroot will go through the class:`Layout` class and all the layout is defined by a single YAML file: ``runroot.yaml``.

Work flow:

#. Command line option ``--run-root``
#. ``$TS_RUNROOT`` Environment variable
#. Look in current directory and look up N (default 4) directories for ``runroot.yaml``
#. Look in executable directory and look up N directories for ``runroot.yaml``.
#. ``$TS_ROOT`` Environment Variable
#. Compiler defaults in layout class 

Right now, the following programs are integrated with the runroot logic:
**traffic_server**, **traffic_manager**, **traffic_ctl**, **traffic_layout**, **traffic_crashlog**, **traffic_logcat**, **traffic_logstat**.

The YAML file
=============

The runroot file (runroot.yaml) can be placed at any location in the filesystem and still be used to define the locations for required |TS| files.
So we can run traffic_server, for example, by ``traffic_server --run-root=/some/directory/runroot.yaml``.

Below is an example of ``runroot.yaml``.

.. code-block:: yaml

    prefix: /home/myname/runroot
    exec_prefix: /home/myname/runroot
    bindir: /home/myname/runroot/bin
    sbindir: /home/myname/runroot/bin
    sysconfdir: /etc/trafficserver
    datadir: /home/myname/runroot/share/trafficserver
    includedir: /home/myname/runroot/include
    libdir: /home/myname/runroot/lib
    libexecdir: /libexec/trafficserver
    localstatedir: /var
    runtimedir: /var/trafficserver
    logdir: /home/myname/runroot/logdir
    cachedir: /var/trafficserver

The path can be both relative or absolute. We can define wherever we want the directory to be. All the items we need to
put into ``runroot.yaml`` are shown above and the entries can be optional. For example, if sysconfdir is not in the file, runroot will
set the sysconfidir, at runtime, to be the default built time sysconfdir concatenated with the prefix.

Runroot management
==================

We can create, remove and verify runroot using ``traffic_layout`` program. It is fully documented in the appendices.

Guide for development
=====================

Basic runroot functionality is handled in ``runroot.cc`` and ``runroot.h`` of ``lib/tscore``. ``runroot_handler()`` and ``argparser_runroot_handler()``
are the main methods for the runroot. The ``Layout`` class will then get the global variable from ``runroot.cc`` to set up the directories properly.

Issue or bug
============

This functionality should be stable and if there is any issue or bug, please report it on the GitHub and @chitianhao.
