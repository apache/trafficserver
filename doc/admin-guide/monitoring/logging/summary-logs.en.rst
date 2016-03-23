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

To generate a summary log file, create a :ref:`LogFormat` object in the
XML-based logging configuration :file:`logs_xml.config` using the SQL-like
aggregate operators below. You can apply each of these operators to specific
fields, over a specified interval.

-  ``COUNT``
-  ``SUM``
-  ``AVERAGE``
-  ``FIRST``
-  ``LAST``

To create a summary log file format:

#. Define the format of the log file in :file:`logs_xml.config` as follows::

       <LogFormat>
         <Name = "summary"/>
         <Format = "%<operator(field)> : %<operator(field)>"/>
         <Interval = "n"/>
       </LogFormat>

   Where ``operator`` is one of the five aggregate operators (``COUNT``,
   ``SUM``, ``AVERAGE``, ``FIRST``, ``LAST``); ``field`` is the logging field
   you want to aggregate; and ``n`` is the interval (in seconds) between
   summary log entries.

   You can specify more than one ``operator`` in the format line. For more
   information, refer to :file:`logs_xml.config`.

#. Run the command :option:`traffic_line -x` to apply configuration changes .

The following example format generates one entry every 10 seconds. Each entry
contains the timestamp of the last entry of the interval, a count of the number
of entries seen within that 10-second interval, and the sum of all bytes sent
to the client::

    <LogFormat>
      <Name = "summary"/>
      <Format = "%<LAST(cqts)> : %<COUNT(*)> : %<SUM(psql)>"/>
      <Interval = "10"/>
    </LogFormat>

.. important::

    You cannot create a format specification that contains
    both aggregate operators and regular fields. For example, the following
    specification would be invalid: ::

        <Format = "%<LAST(cqts)> : %<COUNT(*)> : %<SUM(psql)> : %<cqu>"/>

