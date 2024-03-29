
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

TSEvent
*******

Synopsis
========

.. code-block:: cpp

    #include <ts/apidefs.h>

.. c:type:: TSEvent

Enum typedef defining the possible events which may be passed to a continuation
callback.

Enumeration Members
===================

.. c:macro:: TS_EVENT_NONE

.. c:macro:: TS_EVENT_IMMEDIATE

.. c:macro:: TS_EVENT_TIMEOUT

.. c:macro:: TS_EVENT_ERROR

.. c:macro:: TS_EVENT_CONTINUE

.. c:macro:: TS_EVENT_VCONN_READ_READY

.. c:macro:: TS_EVENT_VCONN_WRITE_READY

.. c:macro:: TS_EVENT_VCONN_READ_COMPLETE

.. c:macro:: TS_EVENT_VCONN_WRITE_COMPLETE

.. c:macro:: TS_EVENT_VCONN_EOS

.. c:macro:: TS_EVENT_VCONN_INACTIVITY_TIMEOUT

.. c:macro:: TS_EVENT_VCONN_ACTIVE_TIMEOUT

.. c:macro:: TS_EVENT_VCONN_START

   An inbound connection has started.

.. c:macro:: TS_EVENT_VCONN_CLOSE

   An inbound connection has closed.

.. c:macro:: TS_EVENT_OUTBOUND_START

   An outbound connection has started.

.. c:macro:: TS_EVENT_OUTBOUND_CLOSE

   An outbound connection has closed.

.. c:macro:: TS_EVENT_NET_CONNECT

.. c:macro:: TS_EVENT_NET_CONNECT_FAILED

.. c:macro:: TS_EVENT_NET_ACCEPT

.. c:macro:: TS_EVENT_NET_ACCEPT_FAILED

.. c:macro:: TS_EVENT_HOST_LOOKUP

.. c:macro:: TS_EVENT_CACHE_OPEN_READ

.. c:macro:: TS_EVENT_CACHE_OPEN_READ_FAILED

.. c:macro:: TS_EVENT_CACHE_OPEN_WRITE

.. c:macro:: TS_EVENT_CACHE_OPEN_WRITE_FAILED

.. c:macro:: TS_EVENT_CACHE_REMOVE

.. c:macro:: TS_EVENT_CACHE_REMOVE_FAILED

.. c:macro:: TS_EVENT_CACHE_SCAN

.. c:macro:: TS_EVENT_CACHE_SCAN_FAILED

.. c:macro:: TS_EVENT_CACHE_SCAN_OBJECT

.. c:macro:: TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED

.. c:macro:: TS_EVENT_CACHE_SCAN_OPERATION_FAILED

.. c:macro:: TS_EVENT_CACHE_SCAN_DONE

.. c:macro:: TS_EVENT_CACHE_LOOKUP

.. c:macro:: TS_EVENT_CACHE_READ

.. c:macro:: TS_EVENT_CACHE_DELETE

.. c:macro:: TS_EVENT_CACHE_WRITE

.. c:macro:: TS_EVENT_CACHE_WRITE_HEADER

.. c:macro:: TS_EVENT_CACHE_CLOSE

.. c:macro:: TS_EVENT_CACHE_LOOKUP_READY

.. c:macro:: TS_EVENT_CACHE_LOOKUP_COMPLETE

.. c:macro:: TS_EVENT_CACHE_READ_READY

.. c:macro:: TS_EVENT_CACHE_READ_COMPLETE

.. c:macro:: TS_EVENT_INTERNAL_1200

.. c:macro:: TS_AIO_EVENT_DONE

.. c:macro:: TS_EVENT_HTTP_CONTINUE

.. c:macro:: TS_EVENT_HTTP_ERROR

.. c:macro:: TS_EVENT_HTTP_READ_REQUEST_HDR

.. c:macro:: TS_EVENT_HTTP_OS_DNS

.. c:macro:: TS_EVENT_HTTP_SEND_REQUEST_HDR

.. c:macro:: TS_EVENT_HTTP_READ_CACHE_HDR

.. c:macro:: TS_EVENT_HTTP_READ_RESPONSE_HDR

.. c:macro:: TS_EVENT_HTTP_SEND_RESPONSE_HDR

.. c:macro:: TS_EVENT_HTTP_REQUEST_TRANSFORM

.. c:macro:: TS_EVENT_HTTP_RESPONSE_TRANSFORM

.. c:macro:: TS_EVENT_HTTP_SELECT_ALT

.. c:macro:: TS_EVENT_HTTP_TXN_START

.. c:macro:: TS_EVENT_HTTP_TXN_CLOSE

.. c:macro:: TS_EVENT_HTTP_SSN_START

.. c:macro:: TS_EVENT_HTTP_SSN_CLOSE

.. c:macro:: TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE

.. c:macro:: TS_EVENT_HTTP_PRE_REMAP

.. c:macro:: TS_EVENT_HTTP_POST_REMAP

.. c:macro:: TS_EVENT_LIFECYCLE_PORTS_INITIALIZED

   The internal data structures for the proxy ports have been initialized.

.. c:macro:: TS_EVENT_LIFECYCLE_PORTS_READY

   The proxy ports are now open for inbound connections.

.. c:macro:: TS_EVENT_LIFECYCLE_CACHE_READY

   The cache is ready.

.. c:macro:: TS_EVENT_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED

.. c:macro:: TS_EVENT_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED

.. c:macro:: TS_EVENT_LIFECYCLE_MSG

   A message from an external source has arrived.

.. c:macro:: TS_EVENT_LIFECYCLE_TASK_THREADS_READY

   The ``ET_TASK`` threads are running.

.. c:macro:: TS_EVENT_LIFECYCLE_SHUTDOWN

   The |TS| process has is shutting down.

.. c:macro:: TS_EVENT_INTERNAL_60200

.. c:macro:: TS_EVENT_INTERNAL_60201

.. c:macro:: TS_EVENT_INTERNAL_60202

.. c:macro:: TS_EVENT_SSL_CERT

   Preparing to present a server certificate to an inbound TLS connection.

.. c:macro:: TS_EVENT_SSL_SERVERNAME

   The SNI name for an Inbound TLS connection has become available.

.. c:macro:: TS_EVENT_SSL_VERIFY_SERVER

   Outbound TLS connection certificate verification (verifying the server certificate).

.. c:macro:: TS_EVENT_SSL_VERIFY_CLIENT

   Inbound TLS connection certificate verification (verifying the client certificate).

.. c:macro:: TS_EVENT_MGMT_UPDATE

Description
===========

These are the event types used to drive continuations in the event system.

.. c:type:: EventType

   The basic category of an event.

.. c:macro:: EVENT_NONE

   A non-specific event.

.. c:macro:: EVENT_IMMEDIATE

   A direct event that is not based on an external event.

.. c:macro:: EVENT_INTERVAL

   An event generated by a time based event.

.. cpp:var:: EventType EVENT_IMMEDIATE

   See :c:macro:`EVENT_IMMEDIATE`.
