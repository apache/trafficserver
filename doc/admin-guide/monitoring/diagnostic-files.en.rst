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

=============
traffic.out
=============

.. logfile:: traffic.out

Debug level messages are written to this file. In particular if a :ref:`debug
tag <proxy.config.diags.debug.tags>` is enabled output for that tag is placed in
this file. This applies to both the |TS| core and plugins. This file also
contains some generic operational messages which track |TS| starting and
stopping. If |TS| crashes a stack trace should be put in this file.

Most messages will have a timestamp, process tag, and a thread pointer.::

   [Oct 13 14:40:46.134] Server {0x2ae95ff5ce80} DIAG: (header_freq.init) initializing plugin

The process tag is "Server" indicating :program:`traffic_server`. It was logged
from the thread with an instance address of `0x2ae95ff5ce80` and had a priority
level of "DIAG" ("diagnostic"). The message is from a plugin, logged because the
debug tag "header_freq.init" was active.

=============
diags.log
=============

.. logfile:: diags.log

Diagnostic output file. Logging messages with a priority higher than "DEBUG" are
sent to this file. If there is an problem that may cause |TS| to malfunction or
not start up this is the first file that should be checked for logging messages.

Messages in this file follow the same general format as for :file:`traffic.out`.

=============
error.log
=============

.. logfile:: error.log

Operational error messages are placed here. These messages are about errors in
transactions, not |TS| itself. For instance if a user agent request is denied
that will be logged here.
