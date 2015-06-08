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

===============
logs_xml.config
===============

.. configfile:: logs_xml.config

The :file:`logs_xml.config` file defines the custom log file formats,
filters, and processing options. The format of this file is modeled
after XML, the Extensible Markup Language.

Format
======

The :file:`logs_xml.config` file contains the specifications below:

-  ``LogFormat`` specifies the fields to be gathered from each protocol
   event access.
-  ``LogFilter`` specifies the filters that are used to include or
   exclude certain entries being logged based on the value of a field
   within that entry.
-  ``LogObject`` specifies an object that contains a particular format,
   a local filename, filters, and collation servers.

The :file:`logs_xml.config` file ignores extra white space, blank lines, and
all comments.

.. _LogFormat:

LogFormat
=========

The following list shows ``LogFormat`` specifications.

.. _LogFormat-name:

``<Name = "valid_format_name"/>``
    Required
    Valid format names include any name except ``squid``, ``common``,
    ``extended``, or ``extended2``, which are pre-defined formats. There
    is no default for this tag. The format object needs to be above the the LogObject object.

.. _LogFormat-Format:

``<Format = "valid_format_specification"/>``
    Required
    A valid format specification is a printf-style string describing
    each log entry when formatted for ASCII output.

    The printf-style could accept Oct/Hex escape representation:

    -  ``\abc`` is Oct escape sequence, a,b,c should be one of [0-9], and
       (a*8^2 + b*8 + c) should be greater than 0 and less than 255.
    -  ``\xab`` is Hex escape sequence, a,b should be one of [0-9, a-f, A-F],
       and (a*16 + b) should be greater than 0 and less than 255.

    Use ``%<`` ``field`` ``>`` as a placeholder for valid field names. For more
    information, refer to :ref:`custom-logging-fields`.

    The specified field can be one of the following types:

    Simple. For example: ``%<cqu>``
    A field within a container, such as an HTTP header or a statistic.
    Fields of this type have the syntax: ::

         %<{ field } container>

    Aggregates, such as ``COUNT``, ``SUM``, ``AVG``, ``FIRST``,
    ``LAST``. Fields of this type have the syntax: ``%<operator (``
    ``field`` ``)>``

.. note::

    You cannot create a format specification that contains both aggregate operators and regular fields.

``<Interval = "aggregate_interval_secs"/>``
    Optional
    Use this tag when the format contains aggregate operators. The value
    "``aggregate_interval_secs``\" represents the number of seconds
    between individual aggregate values being produced.

    The valid set of aggregate operators are:

    -  COUNT
    -  SUM
    -  AVG
    -  FIRST
    -  LAST

.. _LogFilter:

LogFilter
=========

The following list shows the ``LogFilter`` specifications.

``<Name = "valid_filter_name"/>``
    Required
    All filters must be uniquely named.

``<Condition = "valid_log_field valid_operator valid_comparison_value"/>``
    Required
    This field contains the following elements:

    ``valid_log_field`` - the field that will be compared against
    the given value. For more information, refer to :ref:`logging-format-cross-reference`.

    ``valid_operator_field`` - any one of the following: ``MATCH``,
    ``CASE_INSENSITIVE_MATCH``, ``CONTAIN``,
    ``CASE_INSENSITIVE_CONTAIN``.

    -  ``MATCH`` is true if the field and value are identical
       (case-sensitive).
    -  ``CASE_INSENSITIVE_MATCH`` is similar to ``MATCH``, except that
       it is case-insensitive.
    -  ``CONTAIN`` is true if the field contains the value (the value is
       a substring of the field).
    -  ``CASE_INSENSITIVE_CONTAIN`` is a case-insensitive version of
       ``CONTAIN``.

    ``valid_comparison_value`` - any string or integer matching the
    field type. For integer values, all of the operators are equivalent
    and mean that the field must be equal to the specified value.

   For IP address fields, this can be a list of IP addresses and include ranges. A range is an IP address, followed by a
   dash '``-``', and then another IP address of the same family. For instance, the 10/8 network can be represented by
   ``10.0.0.0-10.255.255.255``. Currently network specifiers are not supported.

.. note::

    There are no negative comparison operators. If you want to
    specify a negative condition, then use the ``Action`` field to
    ``REJECT`` the record.

``<Action = "valid_action_field"/>``
    Required: ``ACCEPT`` or ``REJECT`` or ``WIPE_FIELD_VALUE``.
    ACCEPT or REJECT instructs Traffic Server to either accept or reject records
    that satisfy the filter condition. WIPE_FIELD_VALUE wipes out
    the values of the query params in the url fields specified in the Condition.

NOTES: 1. WIPE_FIELD_VALUE action is only applied to the parameters in the query part.
       2. Multiple parameters can be listed in a single WIPE_FIELD_VALUE filter
       3. If the same parameter appears more than once in the query part , only
          the value of the first occurance is wiped

.. _LogObject:

LogObject
=========

The following list shows the ``LogObject`` specifications.

``<Format = "valid_format_name"/>``
    Required
    Valid format names include the predefined logging formats:
    ``squid``, ``common``, ``extended``, and ``extended2``, as well as
    any previously-defined custom log formats. There is no default for
    this tag. The format object needs to be above the the LogObject object.

``<Filename = "file_name"/>``
    Required
    The filename to which the given log file is written on the local
    file system or on a remote collation server. No local log file will
    be created if you fail to specify this tag. All filenames are
    relative to the default logging directory.

    If the name does not contain an extension (for example, ``squid``),
    then the extension ``.log`` is automatically appended to it for
    ASCII logs and ``.blog`` for binary logs (refer to :ref:`Mode =
    "valid_logging_mode" <LogObject-Mode>`).

    If you do not want an extension to be added, then end the filename
    with a single (.) dot (for example: ``squid.`` ).

.. _LogObject-Mode:

``<Mode = "valid_logging_mode"/>``
    Optional
    Valid logging modes include ``ascii`` , ``binary`` , and
    ``ascii_pipe`` . The default is ``ascii`` .

    -  Use ``ascii`` to create event log files in human-readable form
       (plain ASCII).
    -  Use ``binary`` to create event log files in binary format. Binary
       log files generate lower system overhead and occupy less space on
       the disk (depending on the information being logged). You must
       use the :program:`traffic_logcat` utility to translate binary log files to ASCII
       format before you can read them.
    -  Use ``ascii_pipe`` to write log entries to a UNIX named pipe (a
       buffer in memory). Other processes can then read the data using
       standard I/O functions. The advantage of using this option is
       that Traffic Server does not have to write to disk, which frees
       disk space and bandwidth for other tasks. In addition, writing to
       a pipe does not stop when logging space is exhausted because the
       pipe does not use disk space.

    If you are using a collation server, then the log is written to a
    pipe on the collation server. A local pipe is created even before a
    transaction is processed, so you can see the pipe right after
    Traffic Server starts. Pipes on a collation server, however, *are*
    created when Traffic Server starts.

``<Filters = "list_of_valid_filter_names"/>``
    Optional
    A comma-separated list of names of any previously-defined log
    filters. If more than one filter is specified, then all filters must
    accept a record for the record to be logged. The filters need
    to be above the LogObject object.

``<Protocols = "list_of_valid_protocols"/>``
    Optional
    A comma-separated list of the protocols this object should log.
    Valid protocol names for this release are ``HTTP`` (FTP is
    deprecated).

``<ServerHosts = "list_of_valid_servers"/>``
    Optional
    A comma-separated list of valid hostnames.This tag indicates that
    only entries from the named servers will be included in the file.

.. _logs-xml-logobject-collationhost:

``<CollationHosts = "list_of_valid_hostnames:port|failover hosts"/>``
    Optional
    A comma-separated list of collation servers (with pipe delimited
    failover servers) to which all log entries (for this object) are
    forwarded. Collation servers can be specified by name or IP address.
    Specify the collation port with a colon after the name. For example,
    in ``host1:5000|failhostA:5000|failhostB:6000, host2:6000`` logs
    would be sent to host1 and host2, with failhostA and failhostB
    acting as failover hosts for host1. When host1 disconnects,
    logs would be sent to failhostA. If failhostA disconnects, log
    entries would be sent to failhostB until host1 or failhostA comes
    back. Logs would also be sent to host2.

``<Header = "header"/>``
    Optional
    The header text you want the log files to contain. The header text
    appears at the beginning of the log file, just before the first
    record.

``<RollingEnabled = "truth value"/>``
    Optional
    Enables or disables log file rolling for the ``LogObject``. This
    setting overrides the value for the
    :ts:cv:`proxy.config.log.rolling_enabled` variable in the
    :file:`records.config` file. Set *truth value* to one of the
    following values:

    -  ``0`` to disable rolling for this particular ``LogObject``.
    -  ``1`` to roll log files at specific intervals during the day (you
       specify time intervals with the ``RollingIntervalSec`` and
       ``RollingOffsetHr`` fields).
    -  ``2`` to roll log files when they reach a certain size (you
       specify the size with the ``RollingSizeMb`` field).
    -  ``3`` to roll log files at specific intervals during the day or
       when they reach a certain size (whichever occurs first).
    -  ``4`` to roll log files at specific intervals during the day when
       log files reach a specific size (at a specified time if the file
       is of the specified size).

.. XXX this is confusing ^ really, why is it a "truth value" but then it's 5 different integer values that means varias strange things?

``<RollingIntervalSec = "seconds"/>``
    Optional
    The seconds between log file rolling for the ``LogObject``; enables
    you to specify different rolling intervals for different
    ``LogObjects``.

    This setting overrides the value for
    :ts:cv:`proxy.config.log.rolling_interval_sec` in the
    :file:`records.config` file.

``<RollingOffsetHr = "hour"/>``
    Optional
    Specifies an hour (from 0 to 23) at which rolling is guaranteed to
    align. Rolling might start before then, but a rolled file will be
    produced only at that time. The impact of this setting is only
    noticeable if the rolling interval is larger than one hour. This
    setting overrides the configuration setting for
    :ts:cv:`proxy.config.log.rolling_offset_hr` in the :file:`records.config`
    file.

``<RollingSizeMb = "size_in_MB"/>``
    Optional
    The size at which log files are rolled.

Examples
========

The following is an example of a ``LogFormat`` specification that
collects information using three common fields: ::

         <LogFormat>
             <Name="minimal"/>
             <Format = "%<chi> : %<cqu> : %<pssc>"/>
         </LogFormat>

The following is an example of a ``LogFormat`` specification that
uses aggregate operators: ::

         <LogFormat>
             <Name = "summary"/>
             <Format = "%<LAST(cqts)> : %<COUNT(*)> : %<SUM(psql)>"/>
             <Interval = "10"/>
         </LogFormat>

The following is an example of a ``LogFilter`` that will cause only
``REFRESH_HIT`` entries to be logged: ::

         <LogFilter>
              <Name = "only_refresh_hits"/>
              <Action = "ACCEPT"/>
              <Condition = "%<pssc> MATCH REFRESH_HIT"/>
         </LogFilter>

.. note::

    When specifying the field in the filter condition, you can
    omit the ``%<>``. This means that the filter below is equivalent to the
    example directly above: ::

         <LogFilter>
             <Name = "only_refresh_hits"/>
             <Action = "ACCEPT"/>
             <Condition = "pssc MATCH REFRESH_HIT"/>
         </LogFilter>

The following is an example of a ``LogFilter`` that will cause the value of
passwd field be wiped in ``cquc`` ::

         <LogFilter>
             <Name = "wipe_password"/>
             <Condition = "cquc CONTAIN passwd"/>
             <Action = "WIPE_FIELD_VALUE"/>
         </LogFilter>

The following is an example of a ``LogObject`` specification that
creates a local log file for the minimal format defined earlier. The log
filename will be ``minimal.log`` because this is an ASCII log file (the
default).::

         <LogObject>
             <Format = "minimal"/>
             <Filename = "minimal"/>
         </LogObject>

The following is an example of a ``LogObject`` specification that
includes only HTTP requests served by hosts in the domain
``company.com`` or by the specific server ``server.somewhere.com``. Log
entries are sent to port 4000 of the collation host ``logs.company.com``
and to port 5000 of the collation host ``209.131.52.129.`` ::

         <LogObject>
              <Format = "minimal"/>
              <Filename = "minimal"/>
              <ServerHosts = "company.com,server.somewhere.com"/>
              <Protocols = "http"/>
              <CollationHosts = "logs.company.com:4000,209.131.52.129:5000"/>
         </LogObject>

.. _WELF:

WELF
====

Traffic Server supports WELF (WebTrends Enhanced Log Format) so you can
analyze Traffic Server log files with WebTrends reporting tools. A
predefined ``<LogFormat>`` that is compatible with WELF is provided in
the :file:`logs_xml.config` file (shown below). To create a WELF format log
file, create a ``<LogObject>`` that uses this predefined format. ::

         <LogFormat>
             <Name = "welf"/>
             <Format = "id=firewall time=\"%<cqtd> %<cqtt>\" fw=%<phn> pri=6
                proto=%<cqus> duration=%<ttmsf> sent=%<psql> rcvd=%<cqhl>
                src=%<chi> dst=%<shi> dstname=%<shn> user=%<caun> op=%<cqhm>
                arg=\"%<cqup>\" result=%<pssc> ref=\"%<{Referer}cqh>\"
                agent=\"%<{user-agent}cqh>\" cache=%<crc>"/>
         </LogFormat>
