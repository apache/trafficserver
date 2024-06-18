
.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. include:: ../../../common.defs
.. default-domain:: cpp

TSEvent
*******

Synopsis
========

.. code-block:: cpp

    #include <ts/apidefs.h>

.. enum:: TSEvent

Enum typedef defining the possible events which may be passed to a continuation
callback.

Enumeration Members
===================

.. enumerator:: TS_EVENT_NONE

.. enumerator:: TS_EVENT_IMMEDIATE

.. enumerator:: TS_EVENT_TIMEOUT

.. enumerator:: TS_EVENT_ERROR

.. enumerator:: TS_EVENT_CONTINUE

.. enumerator:: TS_EVENT_VCONN_READ_READY

.. enumerator:: TS_EVENT_VCONN_WRITE_READY

.. enumerator:: TS_EVENT_VCONN_READ_COMPLETE

.. enumerator:: TS_EVENT_VCONN_WRITE_COMPLETE

.. enumerator:: TS_EVENT_VCONN_EOS

.. enumerator:: TS_EVENT_VCONN_INACTIVITY_TIMEOUT

.. enumerator:: TS_EVENT_VCONN_ACTIVE_TIMEOUT

.. enumerator:: TS_EVENT_VCONN_START

   An inbound connection has started.

.. enumerator:: TS_EVENT_VCONN_CLOSE

   An inbound connection has closed.

.. enumerator:: TS_EVENT_OUTBOUND_START

   An outbound connection has started.

.. enumerator:: TS_EVENT_OUTBOUND_CLOSE

   An outbound connection has closed.

.. enumerator:: TS_EVENT_NET_CONNECT

.. enumerator:: TS_EVENT_NET_CONNECT_FAILED

.. enumerator:: TS_EVENT_NET_ACCEPT

.. enumerator:: TS_EVENT_NET_ACCEPT_FAILED

.. enumerator:: TS_EVENT_HOST_LOOKUP

.. enumerator:: TS_EVENT_CACHE_OPEN_READ

.. enumerator:: TS_EVENT_CACHE_OPEN_READ_FAILED

.. enumerator:: TS_EVENT_CACHE_OPEN_WRITE

.. enumerator:: TS_EVENT_CACHE_OPEN_WRITE_FAILED

.. enumerator:: TS_EVENT_CACHE_REMOVE

.. enumerator:: TS_EVENT_CACHE_REMOVE_FAILED

.. enumerator:: TS_EVENT_CACHE_SCAN

.. enumerator:: TS_EVENT_CACHE_SCAN_FAILED

.. enumerator:: TS_EVENT_CACHE_SCAN_OBJECT

.. enumerator:: TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED

.. enumerator:: TS_EVENT_CACHE_SCAN_OPERATION_FAILED

.. enumerator:: TS_EVENT_CACHE_SCAN_DONE

.. enumerator:: TS_EVENT_CACHE_LOOKUP

.. enumerator:: TS_EVENT_CACHE_READ

.. enumerator:: TS_EVENT_CACHE_DELETE

.. enumerator:: TS_EVENT_CACHE_WRITE

.. enumerator:: TS_EVENT_CACHE_WRITE_HEADER

.. enumerator:: TS_EVENT_CACHE_CLOSE

.. enumerator:: TS_EVENT_CACHE_LOOKUP_READY

.. enumerator:: TS_EVENT_CACHE_LOOKUP_COMPLETE

.. enumerator:: TS_EVENT_CACHE_READ_READY

.. enumerator:: TS_EVENT_CACHE_READ_COMPLETE

.. enumerator:: TS_EVENT_INTERNAL_1200

.. enumerator:: TS_AIO_EVENT_DONE

.. enumerator:: TS_EVENT_HTTP_CONTINUE

.. enumerator:: TS_EVENT_HTTP_ERROR

.. enumerator:: TS_EVENT_HTTP_READ_REQUEST_HDR

.. enumerator:: TS_EVENT_HTTP_OS_DNS

.. enumerator:: TS_EVENT_HTTP_SEND_REQUEST_HDR

.. enumerator:: TS_EVENT_HTTP_READ_CACHE_HDR

.. enumerator:: TS_EVENT_HTTP_READ_RESPONSE_HDR

.. enumerator:: TS_EVENT_HTTP_SEND_RESPONSE_HDR

.. enumerator:: TS_EVENT_HTTP_REQUEST_TRANSFORM

.. enumerator:: TS_EVENT_HTTP_RESPONSE_TRANSFORM

.. enumerator:: TS_EVENT_HTTP_SELECT_ALT

.. enumerator:: TS_EVENT_HTTP_TXN_START

.. enumerator:: TS_EVENT_HTTP_TXN_CLOSE

.. enumerator:: TS_EVENT_HTTP_SSN_START

.. enumerator:: TS_EVENT_HTTP_SSN_CLOSE

.. enumerator:: TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE

.. enumerator:: TS_EVENT_HTTP_PRE_REMAP

.. enumerator:: TS_EVENT_HTTP_POST_REMAP

.. enumerator:: TS_EVENT_LIFECYCLE_PORTS_INITIALIZED

   The internal data structures for the proxy ports have been initialized.

.. enumerator:: TS_EVENT_LIFECYCLE_PORTS_READY

   The proxy ports are now open for inbound connections.

.. enumerator:: TS_EVENT_LIFECYCLE_CACHE_READY

   The cache is ready.

.. enumerator:: TS_EVENT_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED

.. enumerator:: TS_EVENT_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED

.. enumerator:: TS_EVENT_LIFECYCLE_MSG

   A message from an external source has arrived.

.. enumerator:: TS_EVENT_LIFECYCLE_TASK_THREADS_READY

   The ``ET_TASK`` threads are running.

.. enumerator:: TS_EVENT_LIFECYCLE_SHUTDOWN

   The |TS| process has is shutting down.

.. enumerator:: TS_EVENT_INTERNAL_60200

.. enumerator:: TS_EVENT_INTERNAL_60201

.. enumerator:: TS_EVENT_INTERNAL_60202

.. enumerator:: TS_EVENT_SSL_CERT

   Preparing to present a server certificate to an inbound TLS connection.

.. enumerator:: TS_EVENT_SSL_SERVERNAME

   The SNI name for an Inbound TLS connection has become available.

.. enumerator:: TS_EVENT_SSL_VERIFY_SERVER

   Outbound TLS connection certificate verification (verifying the server certificate).

.. enumerator:: TS_EVENT_SSL_VERIFY_CLIENT

   Inbound TLS connection certificate verification (verifying the client certificate).

.. enumerator:: TS_EVENT_MGMT_UPDATE

Description
===========

These are the event types used to drive continuations in the event system.

.. enum:: EventType

   The basic category of an event.

.. enumerator:: EventType::EVENT_NONE

   A non-specific event.

.. enumerator:: EventType::EVENT_IMMEDIATE

   A direct event that is not based on an external event.

.. enumerator:: EventType::EVENT_INTERVAL

   An event generated by a time based event.
