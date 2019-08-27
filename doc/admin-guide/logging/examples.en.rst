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

.. _admin-logging-examples:

Configuration Examples
**********************

This section provides examples for a wide range of logging scenarios. While not
exhaustive of all possibilities (|TS| logging is quite flexible), these entries
should hopefully get most administrators headed onto the right path.

Unless otherwise noted, the example configurations here are to be applied in
:file:`logging.yaml`.

Online Event Log Builder
========================

If you need any assistance building your event log, you can try out our
`online log builder <https://trafficserver.apache.org/logbuilder/>`_. This is a
work in progress, so any comments, critique or suggestions are most welcome.

Emulating Other HTTP Server Formats
===================================

.. _admin-logging-examples-netscape:

Netscape Common
---------------

The following figure shows a sample log entry in a Netscape Common log file.

.. figure:: /static/images/admin/netscape_common_format.jpg
   :align: center
   :alt: Sample Netscape Common log entry

The numbered sections correspond to the following log fields in |TS|:

=== ====== ====================================================================
No. Field  Description
=== ====== ====================================================================
1   chi    The IP address of the client's host machine.
2   --     This hyphen (``-``) is always present in Netscape log entries.
3   caun   The authenticated client username. A hyphen (``-``) means no
           authentication was required.
4   cqtn   The date and time of the client request, enclosed in brackets.
5   cqtx   The request line, enclosed in quotes.
6   pssc   The proxy response status code (HTTP reply code).
7   pscl   The length of the |TS| response to the client in bytes.
=== ====== ====================================================================

To recreate this as a log format in :file:`logging.yaml` you would define the
following format object:

.. code:: yaml

   formats:
   - name: common
     format: '%<chi> - %<caun> [%<cqtn>] "%<cqtx>" %<pssc> %<pscl>'

.. _admin-logging-examples-extended:

Netscape Extended
-----------------

The following figure shows a sample log entry in a Netscape Extended log file.

.. figure:: /static/images/admin/netscape_extended_format.jpg
   :align: center
   :alt: Sample Netscape Extended log entry

In addition to fields 1-7 from the Netscape Common log format, the Extended
format adds the following fields:

=== ===== =====================================================================
No. Field Description
=== ===== =====================================================================
8   sssc  The origin server response status code.
9   sshl  The server response transfer length; the body length in the
          origin server response to |TS|, in bytes.
10  cqcl  The client request transfer length; the body length in the
          client request to |TS|, in bytes.
11  pqcl  The proxy request transfer length; the body length in the |TS|
          request to the origin server.
12  cqhl  The client request header length; the header length in the
          client request to |TS|.
13  pshl  The proxy response header length; the header length in the
          |TS| response to the client.
14  pqhl  The proxy request header length; the header length in |TS|
          request to the origin server.
15  sshl  The server response header length; the header length in the
          origin server response to |TS|.
16  tts   The time |TS| spent processing the client request; the number
          of seconds between the time that the client established the
          connection with |TS| and the time that |TS| sent the last byte
          of the response back to the client.
=== ===== =====================================================================

To recreate this as a log format in :file:`logging.yaml` you would define the
following format object:

.. code:: yaml

   formats:
   - name: extended
     format: '%<chi> - %<caun> [%<cqtn>] "%<cqtx>" %<pssc> %<pscl> %<sssc> %<sscl> %<cqcl> %<pqcl> %<cqhl> %<pshl> %<pqhl> %<sshl> %<tts>'

.. _admin-logging-examples-extended2:

Netscape Extended-2
-------------------

The following figure shows a sample log entry in a Netscape Extended2 log file.

.. figure:: /static/images/admin/netscape_extended2_format.jpg
   :align: center
   :alt: Sample Netscape Extended-2 log entry

In addition to fields 1-16 from the Netscape Common and Netscape Extended log
formats above, the Extended-2 format adds the following fields:

=== ====== ===============================================================
No.  Field Description
=== ====== ===============================================================
17  phr    The proxy hierarchy route; the route |TS| used to retrieve
           the object.
18  cfsc   The client finish status code: ``FIN`` if the client request
           completed successfully or ``INTR`` if the client request was
           interrupted.
19  pfsc   The proxy finish status code: ``FIN`` if the |TS| request to
           the origin server completed successfully or ``INTR`` if the
           request was interrupted.
20  crc    The cache result code; how the |TS| cache responded to the
           request: ``HIT``, ``MISS``, and so on. Cache result codes are
           listed in :ref:`admin-logging-crc`.
=== ====== ===============================================================

To recreate this as a log format in :file:`logging.yaml` you would define the
following format object:

.. code:: yaml

   formats:
   - name: extended2
     format: '%<chi> - %<caun> [%<cqtn>] "%<cqtx>" %<pssc> %<pscl> %<sssc> %<sscl> %<cqcl> %<pqcl> %<cqhl> %<pshl> %<pqhl> %<sshl> %<tts> %<phr> %<cfsc> %<pfsc> %<crc>'

.. _admin-logging-examples-squid:

Squid
-----

The following figure shows a sample log entry in a Squid log file.

.. figure:: /static/images/admin/squid_format.jpg
   :align: center
   :alt: Sample Squid log entry

The numbered sections correspond to the following log fields in |TS|:

=== ======== ==================================================================
No. Field    Description
=== ======== ==================================================================
1   cqtq     The client request timestamp in Squid format. The time of the
             client request in seconds since January 1, 1970 UTC (with
             millisecond resolution).
2   ttms     The time |TS| spent processing the client request. The number
             of milliseconds between the time the client established the
             connection with |TS| and the time |TS| sent the last byte of
             the response back to the client.
3   chi      The IP address of the client’s host machine.
4   crc/pssc The cache result code; how the cache responded to the request:
             ``HIT``, ``MISS``, and so on. Cache result codes are described
             in :ref:`admin-logging-cache-results`. The
             proxy response status code (HTTP response status code from
             |TS| to client).
5   psql     The length of the |TS| response to the client in bytes,
             including headers and content.
6   cqhm     The client request method: ``GET``, ``POST``, and so on.
7   cauc     The client request canonical URL; blanks and other characters
             that might not be parsed by log analysis tools are replaced by
             escape sequences. The escape sequence is a percentage sign
             followed by the ASCII code number of the replaced character in
             hex.
8   caun     The username of the authenticated client. A hyphen (``-``)
             means that no authentication was required.
9   phr/shn  The proxy hierarchy route. The route |TS| used to retrieve the
             object.
10  psct     The proxy response content type. The object content type taken
             from the |TS| response header.
=== ======== ==================================================================

To recreate this as a log format in :file:`logging.yaml` you would define the
following format object:

.. code:: yaml

   formats:
   - name: squid
     format: '%<cqtq> %<ttms> %<chi> %<crc>/%<pssc> %<psql> %<cqhm> %<cquc> %<caun> %<phr>/%<shn> %<psct>'

Hourly Rotated Squid Proxy Logs
===============================

The following example demonstrates the creation of a Squid-compatible log
format, which is then applied to a log object containing an hourly rotation
policy.

.. code:: yaml

   formats:
   - name: squid
     format: '%<cqtq> %<ttms> %<chi> %<crc>/%<pssc> %<psql> %<cqhm> %<cquc> %<caun> %<phr>/%<shn> %<psct>'

   logs:
   - mode: ascii
     format: squid
     filename: squid
     rolling_enabled: time
     rolling_interval_sec: 3600
     rolling_offset_hr: 0

Summarizing Number of Requests and Total Bytes Sent Every 10 Seconds
====================================================================

The following example format generates one entry every 10 seconds. Each entry
contains the timestamp of the last entry of the interval, a count of the number
of entries seen within that 10-second interval, and the sum of all bytes sent
to clients:

.. code:: yaml

   formats:
   - name: mysummary
     format: '%<LAST(cqts)> : %<COUNT(*)> : %<SUM(psql)>'
     interval: 10

Dual Output to Compact Binary Logs and ASCII Pipes
==================================================

This example demonstrates logging the same event data to multiple locations, in
a hypothetical scenario where we may wish to keep a compact form of our logs
available for archival purposes, while performing live log analysis on a stream
of the event data.

.. code:: yaml

   ourformat = format {
     Format = '%<chi> - %<caun> [%<cqtn>] "%<cqtx>" %<pssc> %<pscl>'
   }

   log.binary {
     Format = ourformat,
     Filename = 'archived_events'
   }
   log.pipe {
     Format = ourformat,
     Filename = 'streaming_log'
   }

Filtering Events to ASCII Pipe for Alerting
===========================================

This example illustrates a situation in which our |TS| cache contains *canary*
objects, which upon their access we want an external alerting system to fire
off all sorts of alarms. To accomplish this, we demonstrate the use of a filter
object that matches events against these particular canaries and emits log data
for them to a UNIX pipe that the alerting software can constantly read from.

.. code:: yaml

   formats:
   - name: canaryformat
     format: '%<chi> - %<caun> [%<cqtn>] "%<cqtx>" %<pssc> %<pscl>'

   filters:
   - name: canaryfilter
     accept: cqup MATCH "/nightmare/scenario/dont/touch"

   logs:
   - mode: pipe
     format: canaryformat
     filters:
     - canaryfilter
     filename: alerting_canaries

Configuring ASCII Pipe Buffer Size
==================================

This example mirrors the one above but also sets a ```pipe_buffer_size``` of
1024 * 1024 for the pipe. This can be set on a per-pipe basis but is not 
available on FreeBSD dists of ATS. If this field is not set, the pipe buffer
will default to the OS default size.

.. code:: yaml

   logs:
   - mode: pipe
     format: canaryformat
     filters:
     - canaryfilter
     filename: alerting_canaries
     pipe_buffer_size: 1048576



Summarizing Origin Responses by Hour
====================================

This example demonstrates a simple use of aggregation operators to produce an
hourly event line reporting on the total number of requests made to origin
servers (where we assume that any cache result code without the string ``HIT``
in it signals origin access), as well as the average time it took to fulfill
the request to clients during that hour.

.. code:: yaml

   logging:
     formats:
     - name: originrepformat
       format: '%<FIRST(cqtq)> %<COUNT(*)> %<AVERAGE(ttms)>'
       interval: 3600

     filters:
     - name: originfilter
       reject: crc CONTAINS "HIT"

     logs:
     - mode: ascii
       format: originrepformat
       filters:
       - originfilter
       filename: origin_access_summary
