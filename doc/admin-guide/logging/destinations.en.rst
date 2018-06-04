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

.. _admin-logging-destinations:

Log Destinations
****************

|TS| enables you to control where event log files are located, if and how they
will be rotated, and how much space they can consume. The first of these topics
is covered in this section, while the latter two will be discussed separately
in :ref:`admin-logging-rotation-retention`.

Two classes of destinations are provided by |TS| currently: local and remote.
Local logging involves storing log data onto filesystems locally mounted on the
same system as the |TS| processes themselves and are covered below in
:ref:`admin-logging-destinations-local`, while remote logging options involving
:manpage:`syslog` and built-in |TS| log collation, are covered below in
:ref:`admin-logging-destinations-remote`.

.. _admin-logging-destinations-local:

Local Logging
=============

.. _admin-logging-directory:

Log Directory Configuration
---------------------------

All local logging output (including incoming collation logs on a |TS| instance
configured as a :ref:`admin-logging-collation-server`) are stored within a
single base directory. Individual log file configurations may optionally append
subdirectories to this base path. This location is adjusted with
:ts:cv:`proxy.config.log.logfile_dir` in :file:`records.config`.

This configuration may specify either an absolute path on the host (if it
begins with ``/``) or a path relative to the |TS| installation directory (any
setting which does not begin with ``/``).

|TS| will need to be restarted, or you will need to run
:option:`traffic_ctl config reload` for changes to the logging directory to
take effect.

Local Log Formats
-----------------

Local |TS| logs may be emitted in three different formats. The optimal format
depends on how administrators intend to use the log data. The first two
options, :ref:`admin-logging-ascii` and :ref:`admin-logging-binary` offer
persistent storage of log data, which may be accessed and analyzed by other
programs at any time (until the log file's configured rotation/retention
policies, as discussed later in :ref:`admin-logging-rotation-retention`).

The third option, :ref:`admin-logging-pipes` offers no persistent storage of
log data, but rather a live stream of logged events which may be read and
interpreted by external processes as they occur.

.. _admin-logging-ascii:

ASCII Log Files
~~~~~~~~~~~~~~~

ASCII |TS| logs are human-readable, plain-text files with output that is easily
read directly and without the required aid of any additional processing or
conversion tools. By default, log files in this format will have a ``.log``
extension.

.. _admin-logging-binary:

Binary Log Files
~~~~~~~~~~~~~~~~

Binary log file output from |TS| avoids the conversion overhead of internal
|TS| data structures to ASCII strings, but any use of these files by external
programs (or just reading by a human) will first require the use of a converter
application. Binary log files by default will have a ``.blog`` file extension.

.. _admin-logging-pipes:

Named Pipes
~~~~~~~~~~~

In addition to ASCII and binary file modes for custom log formats, |TS|
can output log entries in ``ASCII_PIPE`` mode. This mode writes the log entries
to a UNIX named pipe (a buffer in memory). Other processes may read from this
named pipe using standard I/O functions.

The advantage of this mode is that |TS| does not need to write the entries to
disk, which frees disk space and bandwidth for other tasks. When the buffer is
full, |TS| drops log entries and issues an error message indicating how many
entries were dropped. Because |TS| only writes complete log entries to the
pipe, only full records are dropped.

Output to named pipes is always, as the mode's name implies, in ASCII format.
There is no option for logging binary format log data to a named pipe.

.. _admin-logging-ascii-v-binary:

Deciding Between ASCII or Binary Output
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

|TS| offers both ASCII and binary output for log files because each offer
advantages under different circumstances. The primary concerns and trade offs
that should be considered are covered below. Many of the trade offs between
formats will depend heavily on the specific formats you choose for your logs.
To make an accurate determination on whether ASCII or binary logging is better
for your systems, it is recommended that (with good system and performance
monitoring, of course) that you test each format separately under real world
traffic.

The only blanket statement that can really be offered in good conscience is
that ASCII logging *generally* offers a lower path of resistance as no
additional conversion tools will be necessary.

Disk Space
^^^^^^^^^^

ASCII logs tend to consume more disk space than their binary counterparts.
Many numeric fields (e.g. content lengths, HTTP status codes, request and
response times, and so on) as well as string representation of IPv4 and IPv6
addresses will consume more bytes than their binary formats. There are
exceptions (a field containing just the value ``0`` will use a single byte in
an ASCII log, but four bytes in a binary log), so a guarantee cannot be made,
but the general tendency for typical log line formats is to consume slightly
more space in ASCII.

CPU Overhead
^^^^^^^^^^^^

Emitting ASCII format logs does incur some additional processing as the
internal |TS| data structures for relevant transaction details need to be
converted into ASCII strings. While this is usually negligible overhead for
most installations, you may wish to compare the performance overhead between
emitting ASCII or binary log data if you are very concerned with |TS| runtime
performance. By using the binary log format, you may gain a very slight amount
of proxy performance, at the cost of having to invoke an intermediary converter
application every time you wish to view or process the log data.

External Programs
^^^^^^^^^^^^^^^^^

As mentioned above, any use of the log data by other programs will require the
addition of a converter application should you opt for the binary format. If
you are frequently ingesting the log data elsewhere, you may not wish to have
the time and processing cost of this additional step every time.

If the external program is ingesting the logs continuously, you may wish to use
a named pipe output from |TS| instead, which is always in ASCII format, but
doesn't have the potentially increased storage needs as there is no persistent
storage of the log data involved (at least not by |TS| - the application
ingesting the data is probably storing its own results somewhere). It also
avoids unnecessary disk I/O operations if you only care about the final,
analyzed version of the log data and have no permanent use for the intermediate
(and raw) output from |TS|.

Alternatively, if you wish to hyper-optimize your |TS| runtime performance and
are only ingesting the log data with an external application on a batched
schedule, you might consider logging from |TS| using the binary format, then
establishing an externally scheduled one-time conversion of the log data to a
more easily ingested ASCII format into separate file(s). Coordination of this
conversion with the |TS| log rotations would be your responsibility.

.. _admin-logging-destinations-remote:

Remote Logging
==============

|TS| provides for remote log-shipping functionality, which may be used in
addition to or instead of local log storage. This section covers the current
options available.

.. _admin-logging-syslog:

Syslog
------

At this time, |TS| supports sending log data to :manpage:`syslog` only for the
system and emergency logs. Sending custom event or transaction error logs to
syslog is not directly supported. You may use external log aggregation tools,
such as Logstash, to accomplish this by having them handle the ingestion of
|TS| local log files and forwarding to whatever receivers you wish.

.. _admin-logging-collation:

Log Collation
-------------

|TS| offers remote log shipping natively through the log collation feature,
which allows one or more |TS| instances handling regular traffic to transmit
their log data to one or more |TS| instances acting as collation servers.

This allows you to centralize your |TS| logging for (potentially) easier
analysis and reporting in environments with many |TS| instances. Collation
servers may be |TS| instance running a stripped down configuration aimed
at log collation only (and omitting any configuration for actual traffic
proxying or caching).

When a |TS| node generates a buffer of event log entries, it first determines
if it is the collation server or a collation client. The collation server node
writes all log buffers to its local disk (as per its :file:`logging.config`
configuration), just as it would if log collation was not enabled. The
collation client nodes prepare their log buffers for transfer across the
network and send the buffers to the configured log collation server.

If log clients cannot contact their log collation server, then they write their
log buffers to their local disks, into orphan log files. Orphaned log files
require manual collation.

.. important::

    Log collation can have an impact on network performance. Because all nodes
    are forwarding their log data buffers to the single collation server, a
    bottleneck can occur. In addition, collated log files contain timestamp
    information for each entry, but entries in the files do not appear in strict
    chronological order. You may want to sort collated log files before doing
    analysis.

.. _admin-logging-collation-client:

Collation Client
~~~~~~~~~~~~~~~~

To configure a |TS| node to be a collation client, follow the steps below.

#. In the :file:`records.config` file, edit the following variables:

   -  :ts:cv:`proxy.local.log.collation_mode`: ``2`` to configure this node as
      log collation client and send standard formatted log entries to the
      collation server. For custom log entries, see :file:`logging.config`.
   -  :ts:cv:`proxy.config.log.collation_host`
   -  :ts:cv:`proxy.config.log.collation_port`
   -  :ts:cv:`proxy.config.log.collation_secret`
   -  :ts:cv:`proxy.config.log.collation_host_tagged`
   -  :ts:cv:`proxy.config.log.max_space_mb_for_orphan_logs`

#. Run the command :option:`traffic_ctl config reload` to apply the configuration
   changes.

.. note::

    If you modify the collation port or secret after connections between the
    collation server and collation clients have been established, you must
    restart |TS| on all nodes.

.. _admin-logging-collation-server:

Collation Server
~~~~~~~~~~~~~~~~

To configure a |TS| node to be a collation server, perform the following
configuration adjustments in :file:`records.config`:

#. Set :ts:cv:`proxy.local.log.collation_mode` to ``1`` to indicate this node
   will be a server. ::

        CONFIG proxy.local.log.collation_mode INT 1

#. Configure the port on which the server will listen to incoming collation
   transfers from clients, using :ts:cv:`proxy.config.log.collation_port`. If
   omitted, this defaults to port ``8085``. ::

        CONFIG proxy.config.log.collation_port INT 8085

#. Configure the shared secret used by collation clients to authenticate their
   sessions, using :ts:cv:`proxy.config.log.collation_secret`. ::

        CONFIG proxy.config.log.collation_secret STRING seekrit

#. Run the command :option:`traffic_ctl config reload` to apply the configuration
   changes.

.. note::

    If you modify the collation port or secret after connections between the
    collation server and collation clients have been established, you must
    restart |TS| on all nodes.

.. _admin-logging-collating-custom-formats:

Collating Custom Logs
~~~~~~~~~~~~~~~~~~~~~

If you use custom event log files, then you must edit :file:`logging.config`,
in addition to configuring a collation server and collation clients.

To collate custom event log files:

#. On each collation client, edit :file:`logging.config` and add the
   ``CollationHosts`` attribute to the relevant logs. For example, adding two
   collation hosts to an ASCII log that uses the Squid format would look like:

   .. code:: lua

      log.ascii {
        Format = squid,
        Filename = 'squid',
        CollationHosts = { '192.168.1.100:4567', '192.168.1.101:4567' }
      }

#. Run the command :option:`traffic_ctl config reload` to restart |TS|.

