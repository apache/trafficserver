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

.. include:: ../../../common.defs

.. _admin-monitoring-logging-understanding:

Understanding |TS| Log Files
****************************

|TS| records information about every transaction (or request) it processes and
every error it detects in log files. |TS| keeps three types of log files:

-  *Error log files* record information about why a particular
   transaction was in error.

-  *Event log files* (also called *access log files*) record
   information about the state of each transaction Traffic Server
   processes.

-  *System log files* record system information, including messages
   about the state of Traffic Server and errors/warnings it produces.
   This kind of information might include a note that event log files
   were rolled, a warning that cluster communication timed out, or an
   error indicating that Traffic Server was restarted.

   All system information messages are logged with the system-wide
   logging facility :manpage:`syslog` under the daemon facility. The
   :manpage:`syslog.conf(5)` configuration file (stored in the ``/etc`` directory)
   specifies where these messages are logged. A typical location is
   ``/var/log/messages`` (Linux).

   The :manpage:`syslog(8)` process works on a system-wide basis, so it serves as
   the single repository for messages from all Traffic Server processes
   (including :program:`traffic_server`, :program:`traffic_manager`, and
   :program:`traffic_cop`).

   System information logs observe a static format. Each log entry in
   the log contains information about the date and time the error was
   logged, the hostname of the Traffic Server that reported the error,
   and a description of the error or warning.

   Refer to :ref:`admin-monitoring-errors` for a list of the
   messages logged by Traffic Server.

By default, |TS| creates both error and event log files and
records system information in system log files. You can disable event
logging and/or error logging by setting the configuration variable
:ts:cv:`proxy.config.log.logging_enabled` in :file:`records.config`
to one of the following values:

======= =================================================
Value   Description
======= =================================================
``0``   Disable both event and error logging.
``1``   Enable error logging only.
``2``   Enable event logging only.
``3``   Enable both event and error logging.
======= =================================================

By analyzing the log files, you can determine how many people use the |TS|
cache, how much information each person requested, what pages are most popular,
and so on. |TS| supports several standard log file formats, such as Squid and
Netscape, as well as user-defined custom formats. You can analyze the standard
format log files with off-the-shelf analysis packages. To help with log file
analysis, you can separate log files so they contain information specific to
protocol or hosts. You can also configure |TS| to roll log files automatically
at specific intervals during the day or when they reach a certain size.

