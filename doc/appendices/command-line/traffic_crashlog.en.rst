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

.. _traffic_crashlog:

traffic_crashlog
****************

Synopsis
========

:program:`traffic_crashlog` [options]

Description
===========

:program:`traffic_crashlog` is a helper process that catches Traffic Server
crashes and writes a crash report log to the logging directory. Other than for
debugging or development purposes, :program:`traffic_crashlog` is not intended
for users to run directly.

When :ref:`traffic_server` starts, it will launch a :program:`traffic_crashlog`
process and keep it stopped, activating it only if a crash occurs.

Options
=======

.. program:: traffic_crashlog

.. option:: --host TRIPLE

    This option specifies the host triple for the process that
    :program:`traffic_crashlog` should examine. If a supported host
    triple is specified, :program:`traffic_crashlog` expects to
    receive a ``siginfo_t`` structure on it's standard input,
    followed by a ``ucontext_t``.

.. option:: --target PID

    Specifies the process ID of the crashing :program:`traffic_server`
    process. If this option is not specified, :program:`traffic_crashlog`
    assumes it should examine it's parent process.

.. option:: --syslog

    This option causes :program:`traffic_crashlog` to log the name
    of the crash log it creates to the system log.

.. option:: --debug

    This option enables debugging mode. In this mode,
    :program:`traffic_crashlog` emits the log to it's standard
    output.

.. option:: --wait

    This option causes :program:`traffic_crashlog` to stop itself
    immediately after it is launched. :program:`traffic_server`
    will allow it to continue only when a crash occurs.

Caveats
=======

:program:`traffic_crashlog` makes use of various Traffic Server management
APIs. If :ref:`traffic_manager` is not available, the crash log will be
incomplete.

:program:`traffic_crashlog` may generate a crash log containing information you
would rather not share outside your organization. Please examine the crash log
carefully before posting it in a public forum.

See also
========

:manpage:`records.config(5)`,
:manpage:`traffic_manager(8)`,
:manpage:`traffic_server(8)`
