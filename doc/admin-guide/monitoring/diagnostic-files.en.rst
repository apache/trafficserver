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

Debug level messages are written to this file. In particular if a :ts:cv:`debug
tag <proxy.config.diags.debug.tags>` is enabled output for that tag is placed in
this file. This applies to both the |TS| core and plugins. This file also
contains some generic operational messages which track |TS| starting and
stopping. If |TS| crashes a stack trace should be put in this file.

Most messages will have a timestamp, a thread name, and a priority level.::

   [2026-11-06T15:28:48.732Z] [ET_NET 8] DIAG: <HttpSM.cc:4795 (do_remap_request)> (http_seq) [0] Remapping request

The timestamp is ISO 8601 in UTC with milliseconds, so it reads the same on every
host regardless of its time zone. The message was logged from the ``ET_NET 8``
thread and had a priority level of "DIAG" ("diagnostic"). The source location is
shown because of :ts:cv:`proxy.config.diags.show_location`, and the message was
logged because the debug tag "http_seq" was active.

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
