.. _event-logging-formats:

Event Logging Formats
*********************

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

This document provides a reference for all the different logging formats
Traffic Server supports.
Rather than just reading about those formats, you may also want to try our
`online event log builder <http://trafficserver.apache.org/logbuilder/>`_ for an interactive way of
building and understanding log formats.

.. toctree::
   :maxdepth: 2

.. _custom-logging-fields:

Custom Logging Fields
=====================

The following list describes Traffic Server custom logging fields.

.. _cqh:

``{HTTP header field name}cqh``
    Logs the information in the requested field of the client request
    HTTP header. For example, ``%<{Accept-Language}cqh>`` logs the
    ``Accept-Language:`` field in client request headers.

    .. note::
        ecqh is the escaped version of this map

.. _pqh:

``{HTTP header field name}pqh``
    Logs the information in the requested field of the proxy request
    HTTP header. For example, ``%<{Authorization}pqh>`` logs
    the ``Authorization:`` field in proxy request headers.

    .. note::
        epqh is the escaped version of this map

.. _psh:

``{HTTP header field name}psh``
    Logs the information in the requested field of the proxy response
    HTTP header. For example, ``%<{Retry-After}psh>`` logs the
    ``Retry-After:`` field in proxy response headers.

    .. note::
        epsh is the escaped version of this map

.. _ssh:

``{HTTP header field name}ssh``
    Logs the information in the requested field of the server response
    HTTP header. For example, ``%<{Age}ssh>`` logs the ``Age:`` field in
    server response headers.

    .. note::
        essh is the escaped version of this map

.. _cssh:

``{HTTP header field name}cssh``
    Logs the information in the requested field of the cached server response
    HTTP header. For example, ``%<{Age}cssh>`` logs the ``Age:`` field in
    the cached server response headers.

    .. note::
        ecssh is the escaped version of this map

.. _caun:

``caun``
    The client authenticated username; result of the RFC931/ident lookup
    of the client username.

.. _cfsc:

``cfsc``
    The client finish status code; specifies whether the client request
    to Traffic Server was successfully completed (``FIN``) or
    interrupted (``INTR``).

.. _chi:

``chi``
    The IP address of the client's host machine.

.. _chih:

``chih``
    The IP address of the client's host machine in hexadecimal.

.. _chp:

``chp``
    The port number of the client's host machine.

.. _cps:

``cps``
    Client Protocol Stack, the output would be the conjunction of
    protocol names in the stack spliced with '+', such as "TLS+SPDY".

.. _cqbl:

``cqbl``
    The client request transfer length; the body length in the client
    request to Traffic Server (in bytes).

.. _cqhl:

``cqhl``
    The client request header length; the header length in the client
    request to Traffic Server.

.. _cqhm:

``cqhm``
    The HTTP method in the client request to Traffic Server: ``GET``,
    ``POST``, and so on (subset of ``cqtx``).

.. _cqhv:

``cqhv``
    The client request HTTP version.

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
    The client request timestamp, with millisecond resolution.

.. _cqts:

``cqts``
    The client-request timestamp in Squid format; the time of the client
    request since January 1, 1970 UTC. Time is expressed in seconds,
    with millisecond resolution.

.. _cqtt:

``cqtt``
    The client request timestamp. The time of the client request in the
    format hh:mm:ss, where hh is the two-digit hour in 24-hour format,
    mm is the two-digit minutes value, and ss is the 2-digit seconds
    value (for example, 16:01:19).

.. _cqtx:

``cqtx``
    The full HTTP client request text, minus headers; for example, ::

         GET http://www.company.com HTTP/1.0

    In reverse proxy mode, Traffic Server logs the rewritten/mapped URL
    (according to the rules in :file:`remap.config`), _not_ the
    pristine/unmapped URL.

.. _cqu:

``cqu``
    The universal resource identifier (URI) of the request from client
    to Traffic Server (subset of ``cqtx`` ).

    In reverse proxy mode, Traffic Server logs the rewritten/mapped URL
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

.. _pfsc:

``pfsc``
    The proxy finish status code; specifies whether the Traffic Server
    request to the origin server was successfully completed (``FIN``),
    interrupted (``INTR``) or timed out (``TIMEOUT``).

.. _phn:

``phn``
    The hostname of the Traffic Server that generated the log entry in
    collated log files.

.. _phi:

``phi``
    The IP of the Traffic Server that generated the log entry in
    collated log files.

.. _phr:

``phr``
    The proxy hierarchy route; the route Traffic Server used to retrieve
    the object.

.. _php:

``php``
    The TCP port number that Traffic Server served this request from.

.. _piid:

``piid``
   The plugin ID for the transaction. This is set for plugin driven transactions via :c:func:`TSHttpConnectWithPluginId`.

.. _pitag:

``pitag``
   The plugin tag for the transaction. This is set for plugin driven transactions via :c:func:`TSHttpConnectWithPluginId`.

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
    The length of the Traffic Server response to the client (in bytes).

.. _psct:

``psct``
    The content type of the document from server response header: (for
    example, ``img/gif`` ).

.. _pshl:

``pshl``
    The header length in Traffic Server's response to the client.

.. _psql:

``psql``
    The proxy response transfer length in Squid format (includes header
    and content length).

.. _pssc:

``pssc``
    The HTTP response status code from Traffic Server to the client.

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
    The response length (in bytes) from origin server to Traffic Server.

.. _sshl:

``sshl``
    The header length (in bytes) in the origin server response to Traffic Server.

.. _sshv:

``sshv``
    The server response HTTP version (1.0, 1.1, etc.).

.. _sssc:

``sssc``
    The HTTP response status code from origin server to Traffic Server.

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
    The time Traffic Server spends accessing the origin as a fractional
    number of seconds. That is, the time is formated as a floating-point
    number, instead of an integer as in ``stms``.

    For example: if the time is 1500 milliseconds, then this field
    displays 1.5 while the ``stms`` field displays 1500 and the ``sts``
    field displays 1.

.. _sts:

``sts``
    The time Traffic Server spends accessing the origin, in seconds.

.. _ttms:

``ttms``
    The time Traffic Server spends processing the client request; the
    number of milliseconds between the time the client establishes the
    connection with Traffic Server and the time Traffic Server sends the
    last byte of the response back to the client.

.. _ttmsh:

``ttmsh``
    Same as ``ttms`` but in hexadecimal.

.. _ttmsf:

``ttmsf``
    The time Traffic Server spends processing the client request as a
    fractional number of seconds. Time is specified in millisecond
    resolution; however, instead of formatting the output as an integer
    (as with ``ttms``), the display is formatted as a floating-point
    number representing a fractional number of seconds.

    For example: if the time is 1500 milliseconds, then this field
    displays 1.5 while the ``ttms`` field displays 1500 and the ``tts``
    field displays 1.

.. _tts:

``tts``
    The time Traffic Server spends processing the client request; the
    number of seconds between the time at which the client establishes
    the connection with Traffic Server and the time at which Traffic
    Server sends the last byte of the response back to the client.

.. _logging-format-cross-reference:

Logging Format Cross-Reference
==============================

The following sections illustrate the correspondence between Traffic
Server logging fields and standard logging fields for the Squid and
Netscape formats.

Squid Logging Formats
---------------------

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

Netscape Common Logging Formats
-------------------------------

The following is a list of the Netscape Common logging fields and the
corresponding Traffic Server logging field symbols.

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

Netscape Extended Logging Formats
---------------------------------

The following table lists the Netscape Extended logging fields and the
corresponding Traffic Server logging field symbols.

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

Netscape Extended-2 Logging Formats
-----------------------------------

The following is a list of the Netscape Extended-2 logging fields and
the corresponding Traffic Server logging field symbols.

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

.. _log-field-slicing:

Log Field Slicing
=================

It is sometimes desirable to slice a log field to limit the length of a given
log field's output.

Log Field slicing can be specified as below:

``%<field[start:end]>``
``%<{field}container[start:end]>``

Omitting the slice notation defaults to the entire log field.

Slice notation only applies to a log field that is of type string
and can not be applied to ip/timestamp which are converted to
string from integer.

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

  '%<cqup>'       //the whole characters of <cqup>.
  '%<cqup>[:]'    //the whole characters of <cqup>.
  '%<cqup[0:30]>' //the first 30 characters of <cqup>.
  '%<cqup[-10:]>' //the last 10 characters of <cqup>.
  '%<cqup[:-5]>'  //everything except the last 5 characters of <cqup>.
