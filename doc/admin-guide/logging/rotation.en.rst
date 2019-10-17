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

.. _admin-logging-rotation-retention:

Log Rotation and Retention
**************************

Logging is a nearly indispensable part of any networked service, but especially
with high traffic installations care needs to be taken to ensure that log files
don't exhaust storage space and cause maintenance or outage nightmares.

|TS| provides a two-pronged solution: log rotation (also called log rolling) to
keep individual logs as manageable in size as possible for easier ingestion and
analysis by humans and other programs, and log retention to keep logs from
using more space than available and necessary.

This section covers both features.

.. _admin-logging-rotation:

Rotation Options
================

|TS| provides automatic log file rolling. At specific intervals during the day
or when log files reach a certain size, |TS| closes its current set of log
files and opens new log files. Depending on the amount of traffic your servers
are exposed to, you may find that increasing the frequency of log rolling is
beneficial, or even necessary, to maintain manageable log file sets. |TS| nodes
processing moderately high levels of traffic may want to start by rolling logs
every six hours, and adjusting from there.

Log file rolling offers the following benefits:

-  It defines a consistent interval over which log analysis can be performed.

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

-  The original log file's name (such as ``access.log``).

-  The hostname of the |TS| node that generated the log file.

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

The following is an example of a rolled log filename: ::

     access.log.mymachine.20110912.12h00m00s-20000913.12h00m00s.old

The logging system buffers log records before writing them to disk. When
a log file is rolled, the log buffer might be partially full. If it is,
then the first entry in the new log file will have a timestamp earlier
than the time of rolling. When the new log file is rolled, its first
timestamp will be a lower bound for the timestamp of the first entry.

For example, suppose logs are rolled every three hours, and the first
rolled log file is: ::

    access.log.mymachine.20110912.12h00m00s-19980912.03h00m00s.old

If the lower bound for the first entry in the log buffer at 3:00:00 is
2:59:47, then the next log file will have the following timestamp when
rolled: ::

    access.log.mymachine.20110912.02h59m47s-19980912.06h00m00s.old

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

#. Set the minimum number of rolled files with
   :ts:cv:`proxy.config.log.rolling_min_count`. ::

    CONFIG proxy.config.log.rolling_min_count INT 0

#. Run the command :option:`traffic_ctl config reload` to apply the configuration
   changes.

You can fine-tune log file rolling settings for individual log files in the
``log.*`` specification in :file:`logging.yaml`. The custom log file uses the
rolling settings provided in the relevant ``log`` function call, which override
the default settings you specify in Traffic Manager or :file:`records.config`
described above.

.. _admin-logging-retention:

Retention Options
=================

|TS| enables you to control the amount of disk space that the logging directory
can consume. This allows the system to operate smoothly within a specified
space window for a long period of time. After you establish a space limit,
|TS| continues to monitor the space in the logging directory. When the free
space dwindles to the headroom limit, it enters a low space state and takes the
following actions:

-  If the autodelete option is enabled, then |TS| identifies previously-rolled
   log files (log files with the ``.old`` extension). It starts deleting files
   one by one, beginning with the oldest file with largest ratio between current
   number of files and the minimum rolling count, until it emerges from the low
   state. The default minimum rolling count of 0 is treated as INT_MAX during
   ratio calculation. Hence the `rolling_min_count` is not guaranteed to be
   reserved; instead, it is used as a reference to decide the priority of log
   files to delete. In low space state, even when all log files are below minimum
   count, |TS| still tries to delete files until it emerges from the low state.
   |TS| logs a record of all deleted files in the system error log.

-  If the autodelete option is disabled or there are not enough old log files
   to delete for the system to emerge from its low space state, then |TS|
   issues a warning and continues logging until the allocated log space is
   exhausted (which, if configured appropriately, will be well before your
   actual filesystem space is fully consumed and causes additional problems).
   At this point, event logging stops even though proxy traffic is still served
   without client-visible interruption. |TS| resumes event logging when enough
   space becomes available for it to exit the low space state. To make space
   available, either explicitly increase the logging space limit or remove
   files from the logging directory manually.

You can run a :manpage:`cron(8)` script in conjunction with |TS| to
automatically remove old log files from the logging directory before |TS|
enters the low space state. Relocate the old log files to a temporary
partition, where you can run a variety of log analysis scripts. Following
analysis, either compress the logs and move to an archive location, or simply
delete them.

|TS| periodically checks the amount of log space used against both the log
space allocation configured by :ts:cv:`proxy.config.log.max_space_mb_for_logs`
and the actual amount of space available on the disk partition. The used log
space is calculated by summing the size of all files present in the logging
directory and is published in the
:ts:stat:`proxy.process.log.log_files_space_used` metric. The
:ts:cv:`proxy.config.log.max_space_mb_headroom` configuration variable
specifies an amount of headroom that is subtracted from the log space
allocation. This can be tuned to reduce the risk of completely filling the disk
partition.

Setting Log File Management Options
-----------------------------------

To set log management options, follow the steps below:

#. In the :file:`records.config` file, edit the following variables

   -  :ts:cv:`proxy.config.log.max_space_mb_for_logs`
   -  :ts:cv:`proxy.config.log.max_space_mb_headroom`

#. Run the command :option:`traffic_ctl config reload` to apply the configuration
   changes.


Retaining Logs For No More Than a Specified Period
--------------------------------------------------

If for security reasons logs need to be purged to make sure no log entry remains on the box
for more then a specified period of time, we could achieve this by setting the rolling interval,
the maximum number of rolled log files, and forcing |TS| to roll even when there is no traffic.

Let us say we wanted the oldest log entry to be kept on the box to be no older than 2-hour old.

Set :ts:cv:`proxy.config.output.logfile.rolling_interval_sec` (yaml: `rolling_interval_sec`) to 3600 (1h)
which will lead to rolling every 1h.

Set :ts:cv:`proxy.config.output.logfile.rolling_max_count` (yaml: `rolling_max_count`) to 1
which will lead to keeping only one rolled log file at any moment (rolled will be trimmed on every roll).

Set :ts:cv:`proxy.config.output.logfile.rolling_allow_empty` (yaml: `rolling_allow_empty`) to 1 (default: 0)
which will allow logs to be open and rolled even if there was nothing to be logged during the previous period
(i.e. no requests to |TS|).

The above will ensure logs are rolled every 1h hour, only 1 rolled log file to be kept
(rest will be trimmed/removed) and logs will be rolling ("moving") even if nothing is logged
(i.e. no traffic to |TS|).

