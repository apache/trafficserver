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

.. _admin-monitoring-logging-summary-logs:

Summary Logs
************

Due to the speed and efficiency of |TS|, a heavily-loaded node will generate
many events and the event logs can quickly grow to very large sizes.  Using
SQL-like aggregate operators, you can configure |TS| to create summary log
files that summarize a set of log entries over a specified period of time. This
can significantly reduce the size of the log files generated.

To generate a summary log file, create a ``format`` object in the
custom logging configuration :file:`logging.config` using the SQL-like
aggregate operators below. You can apply each of these operators to specific
fields, over a specified interval.

-  ``COUNT``
-  ``SUM``
-  ``AVERAGE``
-  ``FIRST``
-  ``LAST``

Creating Summary Log Formats
============================

To create a summary log file format:

#. Define the format of the log file in :file:`logging.config` as follows:

   .. code:: lua

      mysummary = format {
        Format = "%<operator(field)> : %<operator(field)>",
        Interval = "n"
      }

   Where ``operator`` is one of the five aggregate operators detailed earlier
   and ``field`` is the logging field you want to aggregate. For the interval,
   ``n`` is the interval (in seconds) between summary log entries.

   You can specify more than one ``operator`` in the format line. For more
   information, refer to :file:`logging.config`.

#. Run the command :option:`traffic_ctl config reload` to apply the changes.

Mixing Normal Fields and Aggregates
===================================

It is important to note that you cannot create custom formats for summary logs
which mix both normal, unadorned fields and aggregate functions. Your custom
format must be all of one, or all of the other. In other words, the following
attempt would not work:

.. code:: lua

   wontwork = format {
     Format = "%<LAST(cqts)> : %<COUNT(*)> : %<SUM(psql)> : %<cqu>",
     Interval = "10"
   }

Because it attempts to use the last, count, and sum aggregate functions at the
same time it also includes the bare, unaggregated ``cqu`` field. As each line
of the summary log may be summarized across many individual events, |TS| would
have no way of knowing which individual event's ``cqu`` should be emitted to
the log.

Examples
========

The following example format generates one entry every 10 seconds. Each entry
contains the timestamp of the last entry of the interval, a count of the number
of entries seen within that 10-second interval, and the sum of all bytes sent
to the client:


.. code:: lua

   mysummary = format {
      Format = "%<LAST(cqts)> : %<COUNT(*)> : %<SUM(psql)>",
      Interval = "10"
   }

