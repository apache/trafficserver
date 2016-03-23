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

.. _admin-monitoring-logging-formats:

Log Formats
***********

This document provides a reference for all the different logging formats |TS|
supports. Rather than just reading about those formats, you may also want to
try our `online event log builder <http://trafficserver.apache.org/logbuilder/>`_
for an interactive way of building and understanding log formats.

.. _admin-logging-binary-v-ascii:

Binary or ASCII?
================

You can configure |TS| to create event log files in either of the following:

ASCII
   These files are human-readable and can be processed using standard,
   off-the-shelf log analysis tools. However, |TS| must perform additional
   processing to create the files in ASCII, which mildly impacts system
   overhead. ASCII files also tend to be larger than the equivalent binary
   files. By default, ASCII log files have a ``.log`` filename extension.

Binary
   These files generate lower system overhead and generally occupy less space
   on the disk than ASCII files (depending on the type of information being
   logged). However, you must use a converter application before you can read
   or analyze binary files via standard tools. By default, binary log files use
   a ``.blog`` filename extension.

While binary log files typically require less disk space, there are exceptions.

For example, the value ``0`` (zero) requires only one byte to store in ASCII,
but requires four bytes when stored as a binary integer.  Conversely, if you
define a custom format that logs IP addresses, then a binary log file would
only require four bytes of storage per 32-bit address. However, the same IP
address stored in dot notation would require around 15 characters (bytes) in an
ASCII log file.

It is wise to consider the type of data that will be logged before you select
ASCII or binary for your log files, if your decision is being driven by storage
space concerns. For example, you might try logging to both formats
simultaneously for a representative period of time and compare the storage
requirements of the two logs to determine whether one or the other provides
any measurable savings.

Defining Log Objects
====================

To perform any logging at all on your |TS| nodes, you must have at least one
:ref:`LogObject` defined in :file:`logs_xml.config`. These definitions configure
what logs will be created, the format they will use (covered in the sections
:ref:`admin-monitoring-logging-format-standard` and
:ref:`admin-monitoring-logging-format-custom`), any filters which may be
applied to events before logging them, and when or how the logs will be rolled.

Log Filters
===========

:ref:`LogFilter` objects, configured in :file:`logs_xml.config` allow you to
create filters, which may be applied to :ref:`LogObject` definitions, limiting
the types of entries which will be included in the log output. This may be
useful if your |TS| nodes receive many events which you have no need to log or
analyze.

.. _admin-monitoring-logging-format-standard:

Standard Formats
================

The standard log formats include Squid, Netscape Common, Netscape extended, and
Netscape Extended-2. The standard log file formats can be analyzed with a wide
variety of off-the-shelf log-analysis packages. You should use one of the
standard event log formats unless you need information that these formats do
not provide.

These formats may be used by enabling the :ts:cv:`proxy.config.log.custom_logs_enabled`
setting in :file:`records.config` and adding appropriate entries to
:file:`logs_xml.config`.

.. _admin-logging-format-squid:

Squid
-----

The following figure shows a sample log entry in a Squid log file.

.. figure:: /static/images/admin/squid_format.jpg
   :align: center
   :alt: Sample log entry in squid.log

   Sample log entry in squid.log

====== ========= ==============================================================
Field  Symbol    Description
====== ========= ==============================================================
1      cqtq      The client request timestamp in Squid format. The time of the
                 client request in seconds since January 1, 1970 UTC (with
                 millisecond resolution).
2      ttms      The time |TS| spent processing the client request. The number
                 of milliseconds between the time the client established the
                 connection with |TS| and the time |TS| sent the last byte of
                 the response back to the client.
3      chi       The IP address of the client’s host machine.
4      crc/pssc  The cache result code; how the cache responded to the request:
                 ``HIT``, ``MISS``, and so on. Cache result codes are described
                 in :ref:`admin-monitoring-logging-cache-result-codes`. The
                 proxy response status code (HTTP response status code from
                 |TS| to client).
5      psql      The length of the |TS| response to the client in bytes,
                 including headers and content.
6      cqhm      The client request method: ``GET``, ``POST``, and so on.
7      cauc      The client request canonical URL; blanks and other characters
                 that might not be parsed by log analysis tools are replaced by
                 escape sequences. The escape sequence is a percentage sign
                 followed by the ASCII code number of the replaced character in
                 hex.
8      caun      The username of the authenticated client. A hyphen (``-``)
                 means that no authentication was required.
9      phr/pqsn  The proxy hierarchy route. The route |TS| used to retrieve the
                 object.
10     psct      The proxy response content type. The object content type taken
                 from the |TS| response header.
====== ========= ==============================================================

.. _admin-logging-format-common:

Netscape Common
---------------

The following figure shows a sample log entry in a Netscape Common log file.

.. figure:: /static/images/admin/netscape_common_format.jpg
   :align: center
   :alt: Sample log entry in common.log

   Sample log entry in common.log

====== ========= ==============================================================
Field  Symbol    Description
====== ========= ==============================================================
1      chi       The IP address of the client's host machine.
2      --        This hyphen (``-``) is always present in Netscape log entries.
3      caun      The authenticated client username. A hyphen (``-``) means no
                 authentication was required.
4      cqtd      The date and time of the client request, enclosed in brackets.
5      cqtx      The request line, enclosed in quotes.
6      pssc      The proxy response status code (HTTP reply code).
7      pscl      The length of the |TS| response to the client in bytes.
====== ========= ==============================================================

.. _admin-logging-format-extended:

Netscape Extended
-----------------

The following figure shows a sample log entry in a Netscape Extended log file.

.. figure:: /static/images/admin/netscape_extended_format.jpg
   :align: center
   :alt: Sample log entry in extended.log

   Sample log entry in extended.log

In addition to field 1-7 from the Netscape Common log format above, the
Extended format also adds the following fields:

====== ========= ==============================================================
Field  Symbol    Description
====== ========= ==============================================================
8      sssc      The origin server response status code.
9      sshl      The server response transfer length; the body length in the
                 origin server response to |TS|, in bytes.
10     cqbl      The client request transfer length; the body length in the
                 client request to |TS|, in bytes.
11     pqbl      The proxy request transfer length; the body length in the |TS|
                 request to the origin server.
12     cqhl      The client request header length; the header length in the
                 client request to |TS|.
13     pshl      The proxy response header length; the header length in the
                 |TS| response to the client.
14     pqhl      The proxy request header length; the header length in |TS|
                 request to the origin server.
15     sshl      The server response header length; the header length in the
                 origin server response to |TS|.
16     tts       The time |TS| spent processing the client request; the number
                 of seconds between the time that the client established the
                 connection with |TS| and the time that |TS| sent the last byte
                 of the response back to the client.
====== ========= ==============================================================

.. _admin-logging-format-extended2:

Netscape Extended2
------------------

The following figure shows a sample log entry in a Netscape Extended2 log file.

.. figure:: /static/images/admin/netscape_extended2_format.jpg
   :align: center
   :alt: Sample log entry in extended2.log

   Sample log entry in extended2.log

In addition to field 1-16 from the log formats above, the Extended2 format also adds
the following fields:

====== ======== ===============================================================
Field  Symbol   Description
====== ======== ===============================================================
17     phr      The proxy hierarchy route; the route |TS| used to retrieve
                the object.
18     cfsc     The client finish status code: ``FIN`` if the client request
                completed successfully or ``INTR`` if the client request was
                interrupted.
19     pfsc     The proxy finish status code: ``FIN`` if the |TS| request to
                the origin server completed successfully or ``INTR`` if the
                request was interrupted.
20     crc      The cache result code; how the |TS| cache responded to the
                request: ``HIT``, ``MISS``, and so on. Cache result codes are
                listed in :ref:`admin-monitoring-logging-cache-result-codes`.
====== ======== ===============================================================

.. _admin-monitoring-logging-format-custom:

Custom Formats
==============

Defining a Format
-----------------

Custom logging formats in |TS| are defined by editing :file:`logs_xml.config`
and adding new :ref:`LogFormat` entries for each format you wish to define. The
syntax is fairly simple: every :ref:`LogFormat` element should contain at least
two child elements (additional elements are used for features such as log
summarization and are covered elsewhere):

-  A ``<Name>`` which contains an arbitrary string (using only the allowed
   characters: ``[a-z0-9]``) naming your custom format.

-  A ``<Format>`` which defines the fields that will populate each entry in the
   custom logs, as well as the order in which they appear.

A very simple example format, which contains only the timestamp of when the
event began and the canonical URL of the request, and named *myformat* would
be written as follows::

   <LogFormat>
     <Name = "myformat"/>
     <Format = "%<cqtq> %<cauc>"/>
   </LogFormat>

You may include as many custom field codes as you wish. The full list of codes
available can be found in :ref:`custom-logging-fields`. You may also include
any literal characters in your format. For example, if we wished to separate
the timestamp and canonical URL in our customer format above with a slash
instead of a space, or even a slash surrounded by spaces, we could do so by
just adding the desired characters to the format string::

    %<cqtq> / %<cauc>

You may define as many custom formats as you wish. To apply changes to custom
formats, you will need to run the command :option:`traffic_line -x` after
saving your changes to :file:`logs_xml.config`.

.. _custom-logging-fields:

Custom Logging Fields
---------------------

The following list describes |TS| custom logging fields.

.. _cqh:

``{HTTP header field name}cqh``
    Logs the information in the requested field of the client request
    HTTP header. For example, ``%<{Accept-Language}cqh>`` logs the
    ``Accept-Language:`` field in client request headers.

    .. note::
        ecqh is the URL-encoded version of this map

.. _pqh:

``{HTTP header field name}pqh``
    Logs the information in the requested field of the proxy request
    HTTP header. For example, ``%<{Authorization}pqh>`` logs
    the ``Authorization:`` field in proxy request headers.

    .. note::
        epqh is the URL-encoded version of this map

.. _psh:

``{HTTP header field name}psh``
    Logs the information in the requested field of the proxy response
    HTTP header. For example, ``%<{Retry-After}psh>`` logs the
    ``Retry-After:`` field in proxy response headers.

    .. note::
        epsh is the URL-encoded version of this map

.. _ssh:

``{HTTP header field name}ssh``
    Logs the information in the requested field of the server response
    HTTP header. For example, ``%<{Age}ssh>`` logs the ``Age:`` field in
    server response headers.

    .. note::
        essh is the URL-encoded version of this map

.. _cssh:

``{HTTP header field name}cssh``
    Logs the information in the requested field of the cached server response
    HTTP header. For example, ``%<{Age}cssh>`` logs the ``Age:`` field in
    the cached server response headers.

    .. note::
        ecssh is the URL-encoded version of this map

.. _caun:

``caun``
    The client authenticated username; result of the RFC931/ident lookup
    of the client username.

.. _cfsc:

``cfsc``
    The client finish status code; specifies whether the client request
    to |TS| was successfully completed (``FIN``) or
    interrupted (``INTR``).

.. _chi:

``chi``
    The IP address of the client's host machine.

.. _chih:

``chih``
    The IP address of the client's host machine in hexadecimal.

.. _hii:

``hii``
    This is the incoming (interface) IP address for |TS|, in
    otherwords this is the IP address the client connected to.

.. _hiih:

``hiih``
    The the incoming (interface) IP address in hexadecimal.

.. _chp:

``chp``
    The port number of the client's host machine.

.. _cqbl:

``cqbl``
    The client request transfer length; the body length in the client
    request to |TS| (in bytes).

.. _cqhl:

``cqhl``
    The client request header length; the header length in the client
    request to |TS|.

.. _cqhm:

``cqhm``
    The HTTP method in the client request to |TS|: ``GET``,
    ``POST``, and so on (subset of ``cqtx``).

.. _cqhv:

``cqhv``
    The client request HTTP version.

.. _cqpv:

``cqpv``
    The client request protocol and version.

.. _cqtd:

``cqtd``
    The client request timestamp. Specifies the date of the client
    request in the format yyyy-mm-dd, where yyyy is the 4-digit year, mm
    is the 2-digit month, and dd is the 2-digit day.

.. _cqtn:

``cqtn``
    The client request timestamp; date and time of the client's request
    (in the Netscape timestamp format).

.. _cqtq:

``cqtq``
    The time of the client request since January 1, 1970 UTC (epoch),
    with millisecond resolution.

.. _cqts:

``cqts``
    The time of the client request since January 1, 1970 UTC (EPOCH), with second resolution.

.. _cqtt:

``cqtt``
    The client request timestamp. The time of the client request in the
    format hh:mm:ss, where hh is the two-digit hour in 24-hour format,
    mm is the two-digit minutes value, and ss is the 2-digit seconds
    value (for example, 16:01:19).

.. _cqtr:

``cqtr``
    The TCP reused status; indicates if this client request went through
    an already established connection.

.. _cqssl:

``cqssl``
    The SSL client request status indicates if this client connection
    is over SSL.

.. _cqssr:

``cqssr``
    The SSL session ticket reused status; indicates if this request hit
    the SSL session ticket and avoided a full SSL handshake.

.. _cqssv:

``cqssv``
    The SSL version used to communicate with the client.

.. _cqssc:

``cqssc``
    The cipher used by |TS| to communicate with the client over SSL.

.. _cqtx:

``cqtx``
    The full HTTP client request text, minus headers; for example, ::

         GET http://www.company.com HTTP/1.0

    In reverse proxy mode, |TS| logs the rewritten/mapped URL
    (according to the rules in :file:`remap.config`), _not_ the
    pristine/unmapped URL.

.. _cqu:

``cqu``
    The universal resource identifier (URI) of the request from client
    to |TS| (subset of ``cqtx`` ).

    In reverse proxy mode, |TS| logs the rewritten/mapped URL
    (according to the rules in :file:`remap.config`), _not_ the
    pristine/unmapped URL.

.. _cquc:

``cquc``
    The client request canonical URL. This differs from ``cqu`` in that
    blanks (and other characters that might not be parsed by log
    analysis tools) are replaced by escape sequences. The escape
    sequence is a percentage sign followed by the ASCII code number in
    hex.

    See `cquuc`_.

.. _cqup:

``cqup``
    The client request URL path; specifies the argument portion of the
    URL (everything after the host). For example, if the URL is
    ``http://www.company.com/images/x.gif``, then this field displays
    ``/images/x.gif``

    See `cquup`_.

.. _cqus:

``cqus``
    The client request URL scheme.

.. _cquuc:

``cquuc``
    The client request unmapped URL canonical. This field records a URL
    before it is remapped (reverse proxy mode).

.. _cquup:

``cquup``
    The client request unmapped URL path. This field records a URL path
    before it is remapped (reverse proxy mode).

.. _cquuh:

``cquuh``
    The client request unmapped URL host. This field records a URL's
    host before it is remapped (reverse proxy mode).

.. _cluc:

``cluc``
    The cache lookup URL, or cache key, for the client request. This URL is
    canonicalized as well.

.. _crat:

``crat``
    The Retry-After time in seconds, if specified by the origin server.

.. _crc:

``crc``
    The cache result code; specifies how the cache responded to the
    request (``HIT``, ``MISS``, and so on).

.. _chm:

``chm``
    The cache hit-miss status, specifying which level of the cache this
    was served out of. This is useful for example to show whether it was a
    RAM cache vs disk cache hit. Future versions of the cache will support
    more levels, but right now it only supports RAM (``HIT_RAM``) vs
    rotational disk (``HIT_DISK``).

.. _csscl:

``csscl``
    The cached response length (in bytes) from origin server to Traffic
    Server.

.. _csshl:

``csshl``
    The cached header length in the origin server response to Traffic
    Server (in bytes).

.. _csshv:

``csshv``
    The cached server response HTTP version (1.0, 1.1, etc.).

.. _csssc:

``csssc``
    The cached HTTP response status code from origin server to Traffic
    Server.

.. _cwr:

``cwr``
    The cache write result (``-``, ``WL_MISS``, ``INTR```, ``ERR`` or ``FIN``)

.. _cwtr:

``cwtr``
    The cache write transform result

.. _fsiz:

``fsiz``
    The size of the file (*n* bytes) as seen by the origin server.

.. _ms:

``{Milestone field name}ms``
    The timestamp in milliseconds of a specific milestone for this request.
    see :c:func:`TSHttpTxnMilestoneGet` for milestone names.

.. _msdms:

``{Milestone field name1-Milestone field name2}msdms``
    The difference in milliseconds of between two milestones.
    see :c:func:`TSHttpTxnMilestoneGet` for milestone names.

.. _pfsc:

``pfsc``
    The proxy finish status code; specifies whether the |TS|
    request to the origin server was successfully completed (``FIN``),
    interrupted (``INTR``) or timed out (``TIMEOUT``).

.. _phn:

``phn``
    The hostname of the |TS| that generated the log entry in
    collated log files.

.. _phi:

``phi``
    The IP of the |TS| that generated the log entry in
    collated log files.

.. _phr:

``phr``
    The proxy hierarchy route; the route |TS| used to retrieve
    the object.

.. _php:

``php``
    The TCP port number that |TS| served this request from.

.. _piid:

``piid``
   The plugin ID for the transaction. This is set for plugin driven transactions via :c:func:`TSHttpConnectWithPluginId`.

.. _pitag:

``pitag``
   The plugin tag for the transaction. This is set for plugin driven
   transactions via :c:func:`TSHttpConnectWithPluginId`.

.. _pqbl:

``pqbl``
    The proxy request transfer length; the body length in Traffic
    Server's request to the origin server.

.. _pqhl:

``pqhl``
    The proxy request header length; the header length in Traffic
    Server's request to the origin server.

.. _pqsi:

``pqsi``
    The proxy request server IP address (0 on cache hits and parent-ip
    for requests to parent proxies).

.. _pqsn:

``pqsn``
    The proxy request server name; the name of the server that fulfilled
    the request.

.. _pscl:

``pscl``
    The length of the |TS| response to the client (in bytes).

.. _psct:

``psct``
    The content type of the document from server response header: (for
    example, ``img/gif`` ).

.. _pshl:

``pshl``
    The header length in |TS|'s response to the client.

.. _psql:

``psql``
    The proxy response transfer length in Squid format (includes header
    and content length).

.. _pssc:

``pssc``
    The HTTP response status code from |TS| to the client.

.. _pqssl:

``pqssl``
    Indicates whether the connection from |TS| to the origin
    was over SSL or not.

.. _sca:

``sca``
    The number of attempts in the transaction Traffic Server tries to
    connect to the origin server.

.. _shi:

``shi``
    The IP address resolved from the DNS name lookup of the host in the
    request. For hosts with multiple IP addresses, this field records
    the IP address resolved from that particular DNS lookup.

    This can be misleading for cached documents. For example: if the
    first request was a cache miss and came from *IP1* for server
    *S* and the second request for server *S* resolved to
    *IP2* but came from the cache, then the log entry for the
    second request will show *IP2*.

.. _shn:

``shn``
    The hostname of the origin server.

.. _sscl:

``sscl``
    The response length (in bytes) from origin server to |TS|.

.. _sshl:

``sshl``
    The header length (in bytes) in the origin server response to |TS|.

.. _sshv:

``sshv``
    The server response HTTP version (1.0, 1.1, etc.).

.. _sssc:

``sssc``
    The HTTP response status code from origin server to |TS|.

.. _stms:

``stms``
    The time spent accessing the origin (in milliseconds); the time is
    measured from the time the connection with the origin is established
    to the time the connection is closed.

.. _stmsh:

``stmsh``
    Same as ``stms`` but in hexadecimal.

.. _stmsf:

``stmsf``
    The time |TS| spends accessing the origin as a fractional
    number of seconds. That is, the time is formated as a floating-point
    number, instead of an integer as in ``stms``.

    For example: if the time is 1500 milliseconds, then this field
    displays 1.5 while the ``stms`` field displays 1500 and the ``sts``
    field displays 1.

.. _sts:

``sts``
    The time |TS| spends accessing the origin, in seconds.

.. _sstc:

``sstc``
    The number of transactions between |TS| and the origin server
    from a single server session. A value greater than 0 indicates connection
    reuse.

.. _ttms:

``ttms``
    The time |TS| spends processing the client request; the
    number of milliseconds between the time the client establishes the
    connection with |TS| and the time |TS| sends the
    last byte of the response back to the client.

.. _ttmsh:

``ttmsh``
    Same as ``ttms`` but in hexadecimal.

.. _ttmsf:

``ttmsf``
    The time |TS| spends processing the client request as a
    fractional number of seconds. Time is specified in millisecond
    resolution; however, instead of formatting the output as an integer
    (as with ``ttms``), the display is formatted as a floating-point
    number representing a fractional number of seconds.

    For example: if the time is 1500 milliseconds, then this field
    displays 1.5 while the ``ttms`` field displays 1500 and the ``tts``
    field displays 1.

.. _tts:

``tts``
    The time |TS| spends processing the client request; the
    number of seconds between the time at which the client establishes
    the connection with |TS| and the time at which Traffic
    Server sends the last byte of the response back to the client.

.. _logging-format-cross-reference:

Custom Field Cross-Reference
----------------------------

The following sections illustrate the correspondence between |TS| custom
logging fields and standard logging fields for the Squid and Netscape formats.

Squid
~~~~~

The following is a list of the Squid logging fields and the
corresponding logging field symbols.

============== =============
Squid          Field Symbols
============== =============
time           `cqts`_
elapsed        `ttms`_
client         `chi`_
action/code    `crc`_/`pssc`_
size           `psql`_
method         `cqhm`_
url            `cquc`_
ident          `caun`_
hierarchy/from `phr`_/`pqsn`_
content        `psct`_
============== =============

This is the equivalent XML configuration for the log above::

    <LogFormat>
      <Name = "squid"/>
      <Format = "%<cqtq> %<ttms> %<chi> %<crc>/%<pssc> %<psql> %<cqhm> %<cquc>
                 %<caun> %<phr>/%<pqsn> %<psct>"/>
    </LogFormat>

.. _admin-log-formats-netscape-common:

Netscape Common
~~~~~~~~~~~~~~~

The following is a list of the Netscape Common logging fields and the
corresponding |TS| logging field symbols.

=============== =============
Netscape Common Field Symbols
=============== =============
host            `chi`_
usr             `caun`_
[time]          [`cqtn`_]
"req"           "`cqtx`_"
s1              `pssc`_
c1              `pscl`_
=============== =============

This is the equivalent XML configuration for the log above::

    <LogFormat>
      <Name = "common"/>
      <Format = "%<chi> - %<caun> [%<cqtn>] \"%<cqtx>\" %<pssc> %<pscl>"/>
    </LogFormat>

.. _admin-log-formats-netscape-extended:

Netscape Extended
~~~~~~~~~~~~~~~~~

The following table lists the Netscape Extended logging fields and the
corresponding |TS| logging field symbols.

================= =============
Netscape Extended Field Symbols
================= =============
host              `chi`_
usr               `caun`_
[time]            [`cqtn`_]
"req"             "`cqtx`_"
s1                `pssc`_
c1                `pscl`_
s2                `sssc`_
c2                `sscl`_
b1                `cqbl`_
b2                `pqbl`_
h1                `cqhl`_
h2                `pshl`_
h3                `pqhl`_
h4                `sshl`_
xt                `tts`_
================= =============

This is the equivalent XML configuration for the log above::

    <LogFormat>
      <Name = "extended"/>
      <Format = "%<chi> - %<caun> [%<cqtn>] \"%<cqtx>\" %<pssc> %<pscl>
         %<sssc> %<sscl> %<cqbl> %<pqbl> %<cqhl> %<pshl> %<pqhl> %<sshl> %<tts>"/>
    </LogFormat>

.. _admin-log-formats-netscape-extended2:

Netscape Extended-2
~~~~~~~~~~~~~~~~~~~

The following is a list of the Netscape Extended-2 logging fields and
the corresponding |TS| logging field symbols.

=================== =============
Netscape Extended-2 Field Symbols
=================== =============
``host``            ``chi``
``usr``             ``caun``
``[time]``          ``[cqtn]``
``"req"``           ``"cqtx"``
``s1``              ``pssc``
``c1``              ``pscl``
``s2``              ``sssc``
``c2``              ``sscl``
``b1``              ``cqbl``
``b2``              ``pqbl``
``h1``              ``cqhl``
``h2``              ``pshl``
``h3``              ``pqhl``
``h4``              ``sshl``
``xt``              ``tts``
``route``           ``phr``
``pfs``             ``cfsc``
``ss``              ``pfsc``
``crc``             ``crc``
=================== =============

This is the equivalent XML configuration for the log above::

    <LogFormat>
      <Name = "extended2"/>
      <Format = "%<chi> - %<caun> [%<cqtn>] \"%<cqtx>\" %<pssc> %<pscl>
                 %<sssc> %<sscl> %<cqbl> %<pqbl> %<cqhl> %<pshl> %<pqhl> %<sshl> %<tts> %<phr> %<cfsc> %<pfsc> %<crc>"/>
    </LogFormat>

.. _log-field-slicing:

Log Field Slicing
-----------------

It is sometimes desirable to slice a log field to limit the length of a given
log field's output.

Log Field slicing can be specified as below::

    %<field[start:end]>
    %<{field}container[start:end]>

Omitting the slice notation defaults to the entire log field.

Slice notation only applies to a log field that is of type string and can not
be applied to IPs or timestamp which are converted to string from integer.

The below slice specifiers are allowed.

``[start:end]``
          Log field value from start through end-1
``[start:]``
          Log field value from start through the rest of the string
``[:end]``
          Log field value from the beginning through end-1
``[:]``
          Default - entire Log field

Some examples below ::

  '%<cqup>'       // the whole characters of <cqup>.
  '%<cqup>[:]'    // the whole characters of <cqup>.
  '%<cqup[0:30]>' // the first 30 characters of <cqup>.
  '%<cqup[-10:]>' // the last 10 characters of <cqup>.
  '%<cqup[:-5]>'  // everything except the last 5 characters of <cqup>.

.. _admin-monitoring-logging-cache-result-codes:

Cache Result Codes
==================

The following table describes the cache result codes in Squid and
Netscape log files.

``TCP_HIT``
    A valid copy of the requested object was in the cache and Traffic
    Server sent the object to the client.

``TCP_MISS``
    The requested object was not in cache, so |TS| retrieved
    the object from the origin server (or a parent proxy) and sent it to
    the client.

``TCP_REFRESH_HIT``
    The object was in the cache, but it was stale. |TS| made an
    ``if-modified-since`` request to the origin server and the
    origin server sent a ``304`` not-modified response. Traffic
    Server sent the cached object to the client.

``TCP_REF_FAIL_HIT``
    The object was in the cache but was stale. |TS| made an
    ``if-modified-since`` request to the origin server but the server
    did not respond. |TS| sent the cached object to the
    client.

``TCP_REFRESH_MISS``
    The object was in the cache but was stale. |TS| made an
    ``if-modified-since`` request to the origin server and the server
    returned a new object. |TS| served the new object to the
    client.

``TCP_CLIENT_REFRESH``
    The client issued a request with a ``no-cache`` header. Traffic
    Server obtained the requested object from the origin server and sent
    a copy to the client. |TS| deleted the previous copy of
    the object from cache.

``TCP_IMS_HIT``
    The client issued an ``if-modified-since`` request and the object
    was in cache and fresher than the IMS date, or an
    ``if-modified-since`` request to the origin server revealed the
    cached object was fresh. |TS| served the cached object to
    the client.

``TCP_IMS_MISS``
    The client issued an
    ``if-modified-since request`` and the object was either not in
    cache or was stale in cache. |TS| sent an
    ``if-modified-since request`` to the origin server and received the
    new object. |TS| sent the updated object to the client.

``TCP_SWAPFAIL``
    The object was in the cache but could not be accessed. The client
    did not receive the object.

``ERR_CLIENT_ABORT``
    The client disconnected before the complete object was sent.

``ERR_CONNECT_FAIL``
    |TS| could not reach the origin server.

``ERR_DNS_FAIL``
    The Domain Name Server (DNS) could not resolve the origin server
    name, or no DNS could be reached.

``ERR_INVALID_REQ``
    The client HTTP request was invalid. (|TS| forwards
    requests with unknown methods to the origin server.)

``ERR_READ_TIMEOUT``
    The origin server did not respond to |TS|'s request within
    the timeout interval.

``ERR_PROXY_DENIED``
    Client service was denied.

``ERR_UNKNOWN``
    The client connected, but subsequently disconnected without sending
    a request.
