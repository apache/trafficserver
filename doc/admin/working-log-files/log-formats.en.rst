.. _working-log-files-log-formats:

Log Formats
***********

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

.. _log-formats-squid-format:

Squid Format
============

``1``
    ``cqtq``
    The client request timestamp in Squid format; the time of the client
    request in seconds since January 1, 1970 UTC (with millisecond
    resolution).

``2``
    ``ttms``
    The time Traffic Server spent processing the client request; the
    number of milliseconds between the time the client established the
    connection with Traffic Server and the time Traffic Server sent the
    last byte of the response back to the client.

``3``
    ``chi``
    The IP address of the client’s host machine.

``4``
    ``crc/pssc``
    The cache result code; how the cache responded to the request:
    ``HIT``, ``MISS``, and so on. Cache result codes are described
    :ref:`here <squid-netscape-result-codes>`.
    The proxy response status code (the HTTP response status code from
    Traffic Server to client).

``5``
    ``psql``
    The length of the Traffic Server response to the client in bytes,
    including headers and content.

``6``
    ``cqhm``
    The client request method: ``GET``, ``POST``, and so on.

``7``
    ``cquc``
    The client request canonical URL; blanks and other characters that
    might not be parsed by log analysis tools are replaced by escape
    sequences. The escape sequence is a percentage sign followed by the
    ASCII code number of the replaced character in hex.

``8``
    ``caun``
    The username of the authenticated client. A hyphen (``-``) means
    that no authentication was required.

``9``
    ``phr/pqsn``
    The proxy hierarchy route; the route Traffic Server used to retrieve
    the object.

    The proxy request server name; the name of the server that fulfilled
    the request. If the request was a cache hit, then this field
    contains a hyphen (``-``).

``10``
    ``psct``
    The proxy response content type; the object content type taken from
    the Traffic Server response header.

The following figure shows a sample log entry in a ``squid.log`` file.

.. figure:: ../../static/images/admin/squid_format.jpg
   :align: center
   :alt: Sample log entry in squid.log

   Sample log entry in squid.log

Squid log in XML
----------------

::

    <LogFormat>
      <Name = "squid"/>
      <Format = "%<cqtq> %<ttms> %<chi> %<crc>/%<pssc> %<psql> %<cqhm> %<cquc>
                 %<caun> %<phr>/%<pqsn> %<psct> %<xid>"/>
    </LogFormat>

Netscape Formats
================

Netscape Common
---------------

``1``
    ``chi``
    The IP address of the client’s host machine.

``2``
    ``-``
    This hyphen (``-``) is always present in Netscape log entries.

``3``
    ``caun``
    The authenticated client username. A hyphen (``-``) means no
    authentication was required.

``4``
    ``cqtd``
    The date and time of the client request, enclosed in brackets.

``5``
    ``cqtx``
    The request line, enclosed in quotes.

``6``
    ``pssc``
    The proxy response status code (HTTP reply code).

``7``
    ``pscl``
    The length of the Traffic Server response to the client in bytes.

Netscape Extended
-----------------

``8``
    ``sssc``
    The origin server response status code.

``9``
    ``sshl``
    The server response transfer length; the body length in the origin
    server response to Traffic Server, in bytes.

``10``
    ``cqbl``
    The client request transfer length; the body length in the client
    request to Traffic Server, in bytes.

``11``
    ``pqbl``
    The proxy request transfer length; the body length in the Traffic
    Server request to the origin server.

``12``
    ``cqhl``
    The client request header length; the header length in the client
    request to Traffic Server.

``13``
    ``pshl``
    The proxy response header length; the header length in the Traffic
    Server response to the client.

``14``
    ``pqhl``
    The proxy request header length; the header length in Traffic Server
    request to the origin server.

``15``
    ``sshl``
    The server response header length; the header length in the origin
    server response to Traffic Server.

``16``
    ``tts``
    The time Traffic Server spent processing the client request; the
    number of seconds between the time that the client established the
    connection with Traffic Server and the time that Traffic Server sent
    the last byte of the response back to the client.

Netscape Extended2
------------------

``17``
    ``phr``
    The proxy hierarchy route; the route Traffic Server used to retrieve
    the object.

``18``
    ``cfsc``
    The client finish status code: ``FIN`` if the client request
    completed successfully or ``INTR`` if the client request was
    interrupted.

``19``
    ``pfsc``
    The proxy finish status code: ``FIN`` if the Traffic Server request
    to the origin server completed successfully or ``INTR`` if the
    request was interrupted.

``20``
    ``crc``
    The cache result code; how the Traffic Server cache responded to the
    request: HIT, MISS, and so on. Cache result codes are described
    :ref:`here <squid-netscape-result-codes>`.

Netscape Common
---------------

The following figure shows a sample log entry in a ``common.log`` file,
the list following describes the fields of the format.

.. figure:: ../../static/images/admin/netscape_common_format.jpg
   :align: center
   :alt: Sample log entry in common.log

   Sample log entry in common.log

Netscape Common in XML
~~~~~~~~~~~~~~~~~~~~~~

::

    <LogFormat>
      <Name = "common"/>
      <Format = "%<chi> - %<caun> [%<cqtn>] \"%<cqtx>\" %<pssc> %<pscl>"/>
    </LogFormat>

Netscape Extended
-----------------

The following figure shows a sample log entry in an ``extended.log``
file.

.. figure:: ../../static/images/admin/netscape_extended_format.jpg
   :align: center
   :alt: sample log entry in extended.log

   sample log entry in extended.log

Netscape Extended in XML
~~~~~~~~~~~~~~~~~~~~~~~~

::

    <LogFormat>
      <Name = "extended"/>
      <Format = "%<chi> - %<caun> [%<cqtn>] \"%<cqtx>\" %<pssc> %<pscl> 
         %<sssc> %<sscl> %<cqbl> %<pqbl> %<cqhl> %<pshl> %<pqhl> %<sshl> %<tts>"/>
    </LogFormat>

Netscape Extended2
------------------

The following figure shows a sample log entry in an ``extended2.log``
file.

.. figure:: ../../static/images/admin/netscape_extended2_format.jpg
   :align: center
   :alt: sample log entry in extended2.log

   sample log entry in extended2.log

Netscape Extended in XML
~~~~~~~~~~~~~~~~~~~~~~~~

::

    <LogFormat>
      <Name = "extended2"/>
      <Format = "%<chi> - %<caun> [%<cqtn>] \"%<cqtx>\" %<pssc> %<pscl> 
                 %<sssc> %<sscl> %<cqbl> %<pqbl> %<cqhl> %<pshl> %<pqhl> %<sshl> %<tts> %<phr> %<cfsc> %<pfsc> %<crc>"/>
    </LogFormat>

.. _squid-netscape-result-codes:

Squid- and Netscape-format: Cache Result Codes
==============================================

The following table describes the cache result codes in Squid and
Netscape log files.

``TCP_HIT``
    A valid copy of the requested object was in the cache and Traffic
    Server sent the object to the client.

``TCP_MISS``
    The requested object was not in cache, so Traffic Server retrieved
    the object from the origin server (or a parent proxy) and sent it to
    the client.

``TCP_REFRESH_HIT``
    The object was in the cache, but it was stale. Traffic Server made an 
    ``if-modified-since`` request to the origin server and the
    origin server sent a ``304`` not-modified response. Traffic
    Server sent the cached object to the client.

``TCP_REF_FAIL_HIT``
    The object was in the cache but was stale. Traffic Server made an
    ``if-modified-since`` request to the origin server but the server
    did not respond. Traffic Server sent the cached object to the
    client.

``TCP_REFRESH_MISS``
    The object was in the cache but was stale. Traffic Server made an
    ``if-modified-since`` request to the origin server and the server
    returned a new object. Traffic Server served the new object to the
    client.

``TCP_CLIENT_REFRESH``
    The client issued a request with a ``no-cache`` header. Traffic
    Server obtained the requested object from the origin server and sent
    a copy to the client. Traffic Server deleted the previous copy of
    the object from cache.

``TCP_IMS_HIT``
    The client issued an ``if-modified-since`` request and the object
    was in cache & fresher than the IMS date, **or** an
    ``if-modified-since`` request to the origin server revealed the
    cached object was fresh. Traffic Server served the cached object to
    the client.

``TCP_IMS_MISS``
    The client issued an
    ``if-modified-since request``, and the object was either not in
    cache or was stale in cache. Traffic Server sent an
    ``if-modified-since request`` to the origin server and received the
    new object. Traffic Server sent the updated object to the client.

``TCP_SWAPFAIL``
    The object was in the cache but could not be accessed. The client
    did not receive the object.

``ERR_CLIENT_ABORT``
    The client disconnected before the complete object was sent.

``ERR_CONNECT_FAIL``
    Traffic Server could not reach the origin server.

``ERR_DNS_FAIL``
    The Domain Name Server (DNS) could not resolve the origin server
    name, or no DNS could be reached.

``ERR_INVALID_REQ``
    The client HTTP request was invalid. (Traffic Server forwards
    requests with unknown methods to the origin server.)

``ERR_READ_TIMEOUT``
    The origin server did not respond to Traffic Server's request within
    the timeout interval.

``ERR_PROXY_DENIED``
    Client service was denied.

``ERR_UNKNOWN``
    The client connected, but subsequently disconnected without sending
    a request.


