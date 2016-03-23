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

.. _admin-monitoring-logging-managing:

Managing Logs
*************

|TS| enables you to control where event log files are located and how much
space they can consume. Additionally, you can specify how to handle low disk
space in the logging directory.

Choosing the Logging Directory
==============================

By default, Traffic Server writes all event log files in the ``logs``
directory located in the directory where you installed |TS|. To change this
location, adjust the value of :ts:cv:`proxy.config.log.logfile_dir` in
:file:`records.config`. You will need to either restart |TS| or run the
command :option:`traffic_line -x` for changes to take effect.

Controlling Logging Space
=========================

|TS| enables you to control the amount of disk space that the logging directory
can consume. This allows the system to operate smoothly within a specified
space window for a long period of time.  After you establish a space limit,
|TS| continues to monitor the space in the logging directory. When the free
space dwindles to the headroom limit, it enters a low space state and takes the
following actions:

-  If the autodelete option (discussed in `Rolling Logs`_) is enabled, then
   |TS| identifies previously-rolled log files (log files with the ``.old``
   extension). It starts deleting files one by one, beginning with the oldest
   file, until it emerges from the low state. |TS| logs a record of all deleted
   files in the system error log.

-  If the autodelete option is disabled or there are not enough old log files
   to delete for the system to emerge from its low space state, then |TS|
   issues a warning and continues logging until space is exhausted. When
   available space is consumed, event logging stops. |TS| resumes event logging
   when enough space becomes available for it to exit the low space state. To
   make space available, either explicitly increase the logging space limit or
   remove files from the logging directory manually.

You can run a :manpage:`cron(8)` script in conjunction with |TS| to
automatically remove old log files from the logging directory before |TS|
enters the low space state. Relocate the old log files to a temporary
partition, where you can run a variety of log analysis scripts. Following
analysis, either compress the logs and move to an archive location, or simply
delete them.

Setting Log File Management Options
-----------------------------------

To set log management options, follow the steps below:

#. In the :file:`records.config` file, edit the following variables

   -  :ts:cv:`proxy.config.log.max_space_mb_for_logs`
   -  :ts:cv:`proxy.config.log.max_space_mb_headroom`

#. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

Rolling Logs
============

|TS| provides automatic log file rolling. At specific intervals during the day
or when log files reach a certain size, |TS| closes its current set of log
files and opens new log files. Depending on the amount of traffic your servers
are exposed to, you may find that increasing the frequency of log rolling is
beneficial, or even necessary, to maintain manageable log file sets. |TS| nodes
processing moderately high levels of traffic may want to start by rolling logs
every six hours, and adjusting from there.

Log file rolling offers the following benefits:

-  It defines an consistent interval over which log analysis can be performed.

-  It keeps any single log file from becoming too large and helps to
   keep the logging system within the specified space limits.

-  It provides an easy way to identify files that are no longer being
   used so that an automated script can clean the logging directory and
   run log analysis programs.

Rolled Log Filename Format
--------------------------

|TS| provides a consistent naming scheme for rolled log files that enables you
to easily identify log files. When |TS| rolls a log file, it saves and closes
the old file before it starts a new file. |TS| renames the old file to include
the following information:

-  The format of the file (such as ``squid.log``).

-  The hostname of the |TS| that generated the log file.

-  Two timestamps separated by a hyphen (``-``). The first timestamp is
   a *lower bound* for the timestamp of the first record in the log
   file. The lower bound is the time when the new buffer for log records
   is created. Under low load, the first timestamp in the filename can
   be different from the timestamp of the first entry. Under normal
   load, the first timestamp in the filename and the timestamp of the
   first entry are similar. The second timestamp is an *upper bound*
   for the timestamp of the last record in the log file (this is
   normally the rolling time).

-  The suffix ``.old``, which makes it easy for automated scripts to
   find rolled log files.

Timestamps have the following format: ::

    %Y%M%D.%Hh%Mm%Ss-%Y%M%D.%Hh%Mm%Ss

The following table describes the format:

====== ============================================ ======
Format Description                                  Sample
====== ============================================ ======
``%Y`` The year in four-digit format.               2000
``%M`` The month in two-digit format, from 01-12.   07
``%D`` The day in two-digit format, from 01-31.     19
``%H`` The hour in two-digit format, from 00-23.    21
``%M`` The minute in two-digit format, from 00-59.  52
``%S`` The second in two-digit format, from 00-59.  36
====== ============================================ ======

.. XXX can %S ever be 60, on account of leap seconds, or does ATS have leap-second related issues that otherwise interfere?

The following is an example of a rolled log filename: ::

     squid.log.mymachine.20110912.12h00m00s-20000913.12h00m00s.old

The logging system buffers log records before writing them to disk. When
a log file is rolled, the log buffer might be partially full. If it is,
then the first entry in the new log file will have a timestamp earlier
than the time of rolling. When the new log file is rolled, its first
timestamp will be a lower bound for the timestamp of the first entry.

For example, suppose logs are rolled every three hours, and the first
rolled log file is: ::

    squid.log.mymachine.20110912.12h00m00s-19980912.03h00m00s.old

If the lower bound for the first entry in the log buffer at 3:00:00 is
2:59:47, then the next log file will have the following timestamp when
rolled: ::

    squid.log.mymachine.20110912.02h59m47s-19980912.06h00m00s.old

The contents of a log file are always between the two timestamps. Log
files do not contain overlapping entries, even if successive timestamps
appear to overlap.

Rolling Intervals
-----------------

Log files are rolled at specific intervals relative to a given hour of the day.
Three options may be used to control when log files are rolled:

-  A file size threshold, which will prevent any individual log from growing
   too large.

-  The offset hour, which is an hour between ``0`` (midnight) and ``23``.

-  The rolling interval.

Both the offset hour and the rolling interval determine when log file rolling
starts. Rolling occurs every *rolling interval* and at the *offset* hour. For
example, if the rolling interval is six hours and the offset hour is ``0``
(midnight), then the logs will roll at midnight (00:00), 06:00, 12:00, and
18:00 each day. If the rolling interval is 12 hours and the offset hour is
``3``, then logs will roll at 03:00 and 15:00 each day.

To set log file rolling options and/or configure |TS| to roll log files when
they reach a certain size, adjust the following settings in
:file:`records.config`:

#. Enable log rolling with :ts:cv:`proxy.config.log.rolling_enabled`. ::

    CONFIG proxy.config.log.rolling_enabled INT 1

#. Configure the upper limit on log file size with
   :ts:cv:`proxy.config.log.rolling_size_mb`. ::

    CONFIG proxy.config.log.rolling_size_mb INT 1024

#. Set the offset hour with :ts:cv:`proxy.config.log.rolling_offset_hr`. ::

    CONFIG proxy.config.log.rolling_offset_hr INT 0

#. Set the interval (in seconds) with
   :ts:cv:`proxy.config.log.rolling_interval_sec`. ::

    CONFIG proxy.config.log.rolling_interval_sec INT 21600

#. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

You can fine-tune log file rolling settings for a custom log file in the
:ref:`LogObject` specification in :file:`logs_xml.config`. The custom log file
uses the rolling settings in its :ref:`LogObject`, which override the default
settings you specify in Traffic Manager or :file:`records.config` described
above.

.. _admin-monitoring-logging-host-split:

Separating Logs by Origin
=========================

The default :file:`log_hosts.config` file is located in the |TS| ``config``
directory. To record HTTP transactions for different origin servers in separate
log files, you must specify the hostname of each origin server on a separate
line in :file:`log_hosts.config`. For example, if you specify the keyword
``sports``, then Traffic Server records all HTTP transactions from
``sports.yahoo.com`` and ``www.foxsports.com`` in a log file called
``squid-sports.log`` (if the Squid format is enabled).

.. important::

   If |TS| is clustered and you enable log file collation, then you
   should use the same :file:`log_hosts.config` file on every |TS| node in the
   cluster.

To edit the log hosts list:

#. Enter the hostname of each origin server on a separate line in
   :file:`log_hosts.config`. ::

       webserver1
       webserver2
       webserver3

#. Run the command :option:`traffic_ctl config reload` to apply the changes.

