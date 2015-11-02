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

.. _traffic_cop:

traffic_cop
***********

Description
===========

:program:`traffic_cop` is a watchdog program that is responsible
for starting :ref:`traffic_manager` and :ref:`traffic_server`
and monitoring them for responsiveness. If either of these processes
are determined to be unresponsive, :program:`traffic_cop` will kill
and restart them.

On Linux, :program:`traffic_cop` will also monitor available memory
and swap space, restarting the watched processes if the available
memory falls below a minimum threshold. The memory thresholds can
be configured with the :ts:cv:`proxy.config.cop.linux_min_swapfree_kb`
and :ts:cv:`proxy.config.cop.linux_min_memfree_kb` variables.

Options
=======

.. program:: traffic_cop

.. option:: -d, --debug

   Emit debugging messages.

.. option:: -o, --stdout

  :program:`traffic_cop` ordinarily logs to syslog, however for
  debugging purposes, this option causes it to print messages to
  standard output instead.

.. option:: -s, --stop

   Kill children using ``SIGSTOP`` instead of ``SIGKILL``. This
   option is primarily for debugging.

.. option:: -V, --version

   Print version information and exit.

See also
========

:manpage:`syslog(1)`,
:manpage:`traffic_manager(8)`,
:manpage:`traffic_server(8)`

