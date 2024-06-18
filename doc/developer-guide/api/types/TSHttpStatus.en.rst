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

TSHttpStatus
************

Synopsis
========

.. code-block:: cpp

    #include <ts/apidefs.h>

.. enum:: TSHttpStatus

This set of enums represents the possible HTTP types that can be
assigned to an HTTP header.

When a header is created with :func:`TSHttpHdrCreate`, it is
automatically assigned a type of :cpp:enumerator:`TS_HTTP_TYPE_UNKNOWN`.  You
can modify the HTTP type ONCE after it the header is created, using
:func:`TSHttpHdrTypeSet`.  After setting the HTTP type once, you
cannot set it again.  Use :func:`TSHttpHdrTypeGet` to obtain the
:type:`TSHttpType` of an HTTP header.

Enumeration Members
===================

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_NONE

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_CONTINUE

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_SWITCHING_PROTOCOL

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_EARLY_HINTS

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_OK

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_CREATED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_ACCEPTED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_NO_CONTENT

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_RESET_CONTENT

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_PARTIAL_CONTENT

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_MULTI_STATUS

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_ALREADY_REPORTED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_IM_USED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_MULTIPLE_CHOICES

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_MOVED_PERMANENTLY

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_MOVED_TEMPORARILY

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_SEE_OTHER

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_NOT_MODIFIED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_USE_PROXY

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_TEMPORARY_REDIRECT

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_PERMANENT_REDIRECT

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_BAD_REQUEST

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_UNAUTHORIZED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_PAYMENT_REQUIRED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_FORBIDDEN

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_NOT_FOUND

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_METHOD_NOT_ALLOWED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_NOT_ACCEPTABLE

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_REQUEST_TIMEOUT

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_CONFLICT

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_GONE

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_LENGTH_REQUIRED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_PRECONDITION_FAILED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_REQUEST_URI_TOO_LONG

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_EXPECTATION_FAILED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_UNPROCESSABLE_ENTITY

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_LOCKED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_FAILED_DEPENDENCY

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_UPGRADE_REQUIRED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_PRECONDITION_REQUIRED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_TOO_MANY_REQUESTS

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_INTERNAL_SERVER_ERROR

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_NOT_IMPLEMENTED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_BAD_GATEWAY

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_SERVICE_UNAVAILABLE

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_GATEWAY_TIMEOUT

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_HTTPVER_NOT_SUPPORTED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_VARIANT_ALSO_NEGOTIATES

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_INSUFFICIENT_STORAGE

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_LOOP_DETECTED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_NOT_EXTENDED

.. enumerator:: TSHttpStatus::TS_HTTP_STATUS_NETWORK_AUTHENTICATION_REQUIRED

Description
===========
