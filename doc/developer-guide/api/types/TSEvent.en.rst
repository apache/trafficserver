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

`#include <ts/apidefs.h>`

.. c:type:: TSEvent

Enum typedef defining the possible events which may be passed to a continuation
callback.

Enumeration Members
===================

.. c:member:: TSEvent TS_EVENT_NONE

.. c:member:: TSEvent TS_EVENT_IMMEDIATE

.. c:member:: TSEvent TS_EVENT_TIMEOUT

.. c:member:: TSEvent TS_EVENT_ERROR

.. c:member:: TSEvent TS_EVENT_CONTINUE

.. c:member:: TSEvent TS_EVENT_VCONN_READ_READY

.. c:member:: TSEvent TS_EVENT_VCONN_WRITE_READY

.. c:member:: TSEvent TS_EVENT_VCONN_READ_COMPLETE

.. c:member:: TSEvent TS_EVENT_VCONN_WRITE_COMPLETE

.. c:member:: TSEvent TS_EVENT_VCONN_EOS

.. c:member:: TSEvent TS_EVENT_VCONN_INACTIVITY_TIMEOUT

.. c:member:: TSEvent TS_EVENT_VCONN_ACTIVE_TIMEOUT

.. c:member:: TSEvent TS_EVENT_NET_CONNECT

.. c:member:: TSEvent TS_EVENT_NET_CONNECT_FAILED

.. c:member:: TSEvent TS_EVENT_NET_ACCEPT

.. c:member:: TSEvent TS_EVENT_NET_ACCEPT_FAILED

.. c:member:: TSEvent TS_EVENT_INTERNAL_206

.. c:member:: TSEvent TS_EVENT_INTERNAL_207

.. c:member:: TSEvent TS_EVENT_INTERNAL_208

.. c:member:: TSEvent TS_EVENT_INTERNAL_209

.. c:member:: TSEvent TS_EVENT_INTERNAL_210

.. c:member:: TSEvent TS_EVENT_INTERNAL_211

.. c:member:: TSEvent TS_EVENT_INTERNAL_212

.. c:member:: TSEvent TS_EVENT_HOST_LOOKUP

.. c:member:: TSEvent TS_EVENT_CACHE_OPEN_READ

.. c:member:: TSEvent TS_EVENT_CACHE_OPEN_READ_FAILED

.. c:member:: TSEvent TS_EVENT_CACHE_OPEN_WRITE

.. c:member:: TSEvent TS_EVENT_CACHE_OPEN_WRITE_FAILED

.. c:member:: TSEvent TS_EVENT_CACHE_REMOVE

.. c:member:: TSEvent TS_EVENT_CACHE_REMOVE_FAILED

.. c:member:: TSEvent TS_EVENT_CACHE_SCAN

.. c:member:: TSEvent TS_EVENT_CACHE_SCAN_FAILED

.. c:member:: TSEvent TS_EVENT_CACHE_SCAN_OBJECT

.. c:member:: TSEvent TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED

.. c:member:: TSEvent TS_EVENT_CACHE_SCAN_OPERATION_FAILED

.. c:member:: TSEvent TS_EVENT_CACHE_SCAN_DONE

.. c:member:: TSEvent TS_EVENT_CACHE_LOOKUP

.. c:member:: TSEvent TS_EVENT_CACHE_READ

.. c:member:: TSEvent TS_EVENT_CACHE_DELETE

.. c:member:: TSEvent TS_EVENT_CACHE_WRITE

.. c:member:: TSEvent TS_EVENT_CACHE_WRITE_HEADER

.. c:member:: TSEvent TS_EVENT_CACHE_CLOSE

.. c:member:: TSEvent TS_EVENT_CACHE_LOOKUP_READY

.. c:member:: TSEvent TS_EVENT_CACHE_LOOKUP_COMPLETE

.. c:member:: TSEvent TS_EVENT_CACHE_READ_READY

.. c:member:: TSEvent TS_EVENT_CACHE_READ_COMPLETE

.. c:member:: TSEvent TS_EVENT_INTERNAL_1200

.. c:member:: TSEvent TS_AIO_EVENT_DONE

.. c:member:: TSEvent TS_EVENT_HTTP_CONTINUE

.. c:member:: TSEvent TS_EVENT_HTTP_ERROR

.. c:member:: TSEvent TS_EVENT_HTTP_READ_REQUEST_HDR

.. c:member:: TSEvent TS_EVENT_HTTP_OS_DNS

.. c:member:: TSEvent TS_EVENT_HTTP_SEND_REQUEST_HDR

.. c:member:: TSEvent TS_EVENT_HTTP_READ_CACHE_HDR

.. c:member:: TSEvent TS_EVENT_HTTP_READ_RESPONSE_HDR

.. c:member:: TSEvent TS_EVENT_HTTP_SEND_RESPONSE_HDR

.. c:member:: TSEvent TS_EVENT_HTTP_REQUEST_TRANSFORM

.. c:member:: TSEvent TS_EVENT_HTTP_RESPONSE_TRANSFORM

.. c:member:: TSEvent TS_EVENT_HTTP_SELECT_ALT

.. c:member:: TSEvent TS_EVENT_HTTP_TXN_START

.. c:member:: TSEvent TS_EVENT_HTTP_TXN_CLOSE

.. c:member:: TSEvent TS_EVENT_HTTP_SSN_START

.. c:member:: TSEvent TS_EVENT_HTTP_SSN_CLOSE

.. c:member:: TSEvent TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE

.. c:member:: TSEvent TS_EVENT_HTTP_PRE_REMAP

.. c:member:: TSEvent TS_EVENT_HTTP_POST_REMAP

.. c:member:: TSEvent TS_EVENT_LIFECYCLE_PORTS_INITIALIZED

.. c:member:: TSEvent TS_EVENT_LIFECYCLE_PORTS_READY

.. c:member:: TSEvent TS_EVENT_LIFECYCLE_CACHE_READY

.. c:member:: TSEvent TS_EVENT_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED

.. c:member:: TSEvent TS_EVENT_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED

.. c:member:: TSEvent TS_EVENT_VCONN_PRE_ACCEPT

.. c:member:: TSEvent TS_EVENT_MGMT_UPDATE

.. c:member:: TSEvent TS_EVENT_INTERNAL_60200

.. c:member:: TSEvent TS_EVENT_INTERNAL_60201

.. c:member:: TSEvent TS_EVENT_INTERNAL_60202

Description
===========

