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

``{HTTP header field name}cqh``
    Logs the information in the requested field of the client request
    HTTP header. For example, ``%<{Accept-Language}cqh>`` logs the
    ``Accept-Language:`` field in client request headers.

``{HTTP header field name}pqh``
    Logs the information in the requested field of the proxy request
    HTTP header. For example, ``%<{Authorization}pqh>`` logs
    the ``Authorization:`` field in proxy request headers.

``{HTTP header field name}psh``
    Logs the information in the requested field of the proxy response
    HTTP header. For example, ``%<{Retry-After}psh>`` logs the
    ``Retry-After:`` field in proxy response headers.

``{HTTP header field name}ssh``
    Logs the information in the requested field of the server response
    HTTP header. For example, ``%<{Age}ssh>`` logs the ``Age:`` field in
    server response headers.

``caun``
    The client authenticated username; result of the RFC931/ident lookup
    of the client username.

``cfsc``
    The client finish status code; specifies whether the client request
    to Traffic Server was successfully completed (``FIN``) or
    interrupted (``INTR``).

``chi``
    The IP address of the client's host machine.

``chih``
    The IP address of the client's host machine in hexadecimal.

``chp``
    The port number of the client's host machine.

``cqbl``
    The client request transfer length; the body length in the client
    request to Traffic Server (in bytes).

``cqhl``
    The client request header length; the header length in the client
    request to Traffic Server.

``cqhm``
    The HTTP method in the client request to Traffic Server: ``GET``,
    ``POST``, and so on (subset of ``cqtx``).

``cqhv``
    The client request HTTP version.

``cqtd``
    The client request timestamp. Specifies the date of the client
    request in the format yyyy-mm-dd, where yyyy is the 4-digit year, mm
    is the 2-digit month, and dd is the 2-digit day.

``cqtn``
    The client request timestamp; date and time of the client's request
    (in the Netscape timestamp format).

``cqtq``
    The client request timestamp, with millisecond resolution.

``cqts``
    The client-request timestamp in Squid format; the time of the client
    request since January 1, 1970 UTC. Time is expressed in seconds,
    with millisecond resolution.

``cqtt``
    The client request timestamp. The time of the client request in the
    format hh:mm:ss, where hh is the two-digit hour in 24-hour format,
    mm is the two-digit minutes value, and ss is the 2-digit seconds
    value (for example, 16:01:19).

``cqtx``
    The full HTTP client request text, minus headers; for example, ::

         GET http://www.company.com HTTP/1.0

    In reverse proxy mode, Traffic Server logs the rewritten/mapped URL
    (according to the rules in the
    :file:`remap.config` file), _not_ the pristine/unmapped URL.

``cqu``
    The universal resource identifier (URI) of the request from client
    to Traffic Server (subset of ``cqtx`` ).

    In reverse proxy mode, Traffic Server logs the rewritten/mapped URL
    (according to the rules in the
    :file:`remap.config` file),
    _not_ the pristine/unmapped URL.

``cquc``
    The client request canonical URL. This differs from ``cqu`` in that
    blanks (and other characters that might not be parsed by log
    analysis tools) are replaced by escape sequences. The escape
    sequence is a percentage sign followed by the ASCII code number in
    hex.

    See `cquuc`_.

``cqup``
    The client request URL path; specifies the argument portion of the
    URL (everything after the host). For example, if the URL is
    ``http://www.company.com/images/x.gif``, then this field displays
    ``/images/x.gif``

    See `cquup`_.

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

``crat``
    The Retry-After time in seconds, if specified by the origin server.

``crc``
    The cache result code; specifies how the cache responded to the
    request (``HIT``, ``MISS``, and so on).

``csscl``
    The cached response length (in bytes) from origin server to Traffic
    Server.

``csshl``
    The cached header length in the origin server response to Traffic
    Server (in bytes).

``csshv``
    The cached server response HTTP version (1.0, 1.1, etc.).

``csssc``
    The cached HTTP response status code from origin server to Traffic
    Server.

``cwr``
    The cache write result (``-``, ``WL_MISS``, ``INTR```, ``ERR`` or ``FIN``)

``cwtr``
    The cache write transform result

``fsiz``
    The size of the file (*n* bytes) as seen by the origin server.

``pfsc``
    The proxy finish status code; specifies whether the Traffic Server
    request to the origin server was successfully completed (``FIN``),
    interrupted (``INTR``) or timed out (``TIMEOUT``).

``phn``
    The hostname of the Traffic Server that generated the log entry in
    collated log files.

``phi``
    The IP of the Traffic Server that generated the log entry in
    collated log files.

``phr``
    The proxy hierarchy route; the route Traffic Server used to retrieve
    the object.

``pqbl``
    The proxy request transfer length; the body length in Traffic
    Server's request to the origin server.

``pqhl``
    The proxy request header length; the header length in Traffic
    Server's request to the origin server.

``pqsi``
    The proxy request server IP address (0 on cache hits and parent-ip
    for requests to parent proxies).

``pqsn``
    The proxy request server name; the name of the server that fulfilled
    the request.

``pscl``
    The length of the Traffic Server response to the client (in bytes).

``psct``
    The content type of the document from server response header: (for
    example, ``img/gif`` ).

``pshl``
    The header length in Traffic Server's response to the client.

``psql``
    The proxy response transfer length in Squid format (includes header
    and content length).

``pssc``
    The HTTP response status code from Traffic Server to the client.

``shi``
    The IP address resolved from the DNS name lookup of the host in the
    request. For hosts with multiple IP addresses, this field records
    the IP address resolved from that particular DNS lookup.

    This can be misleading for cached documents. For example: if the
    first request was a cache miss and came from **``IP1``** for server
    **``S``** and the second request for server **``S``** resolved to
    **``IP2``** but came from the cache, then the log entry for the
    second request will show **``IP2``**.

``shn``
    The hostname of the origin server.

``sscl``
    The response length (in bytes) from origin server to Traffic Server.

``sshl``
    The header length in the origin server response to Traffic Server
    (in bytes).

``sshv``
    The server response HTTP version (1.0, 1.1, etc.).

``sssc``
    The HTTP response status code from origin server to Traffic Server.

``ttms``
    The time Traffic Server spends processing the client request; the
    number of milliseconds between the time the client establishes the
    connection with Traffic Server and the time Traffic Server sends the
    last byte of the response back to the client.

``ttmsh``
    Same as ``ttms`` but in hexadecimal.

``ttmsf``
    The time Traffic Server spends processing the client request as a
    fractional number of seconds. Time is specified in millisecond
    resolution; however, instead of formatting the output as an integer
    (as with ``ttms``), the display is formatted as a floating-point
    number representing a fractional number of seconds.

    For example: if the time is 1500 milliseconds, then this field
    displays 1.5 while the ``ttms`` field displays 1500 and the ``tts``
    field displays 1.

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

================== =============
Squid              Field Symbols
================== =============
``time``           ``cqts``
``elapsed``        ``ttms``
``client``         ``chi``
``action/code``    ``crc/pssc``
``size``           ``psql``
``method``         ``cqhm``
``url``            ``cquc``
``ident``          ``caun``
``hierarchy/from`` ``phr/pqsn``
``content``        ``psct``
================== =============

Netscape Common Logging Formats
-------------------------------

The following is a list of the Netscape Common logging fields and the
corresponding Traffic Server logging field symbols.

=============== =============
Netscape Common Field Symbols
=============== =============
``host``        ``chi``
``usr``         ``caun``
``[time]``      ``[cqtn]``
``"req"``       ``"cqtx"``
``s1``          ``pssc``
``c1``          ``pscl``
=============== =============

Netscape Extended Logging Formats
---------------------------------

The following table lists the Netscape Extended logging fields and the
corresponding Traffic Server logging field symbols.

================= =============
Netscape Extended Field Symbols
================= =============
``host``          ``chi``
``usr``           ``caun``
``[time]``        ``[cqtn]``
``"req"``         ``"cqtx"``
``s1``            ``pssc``
``c1``            ``pscl``
``s2``            ``sssc``
``c2``            ``sscl``
``b1``            ``cqbl``
``b2``            ``pqbl``
``h1``            ``cqhl``
``h2``            ``pshl``
``h3``            ``pqhl``
``h4``            ``sshl``
``xt``            ``tts``
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

