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

.. default-domain:: c

TSHttpStatus
************

Synopsis
========

`#include <ts/apidefs.h>`

.. c:type:: TSHttpStatus

This set of enums represents the possible HTTP types that can be
assigned to an HTTP header.

When a header is created with :func:`TSHttpHdrCreate`, it is
automatically assigned a type of :data:`TS_HTTP_TYPE_UNKNOWN`.  You
can modify the HTTP type ONCE after it the header is created, using
:func:`TSHttpHdrTypeSet`.  After setting the HTTP type once, you
cannot set it again.  Use :func:`TSHttpHdrTypeGet` to obtain the
:type:`TSHttpType` of an HTTP header.

Enumeration Members
===================

.. c:member:: TSHttpStatus TS_HTTP_STATUS_NONE

.. c:member:: TSHttpStatus TS_HTTP_STATUS_CONTINUE

.. c:member:: TSHttpStatus TS_HTTP_STATUS_SWITCHING_PROTOCOL

.. c:member:: TSHttpStatus TS_HTTP_STATUS_EARLY_HINTS

.. c:member:: TSHttpStatus TS_HTTP_STATUS_OK

.. c:member:: TSHttpStatus TS_HTTP_STATUS_CREATED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_ACCEPTED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION

.. c:member:: TSHttpStatus TS_HTTP_STATUS_NO_CONTENT

.. c:member:: TSHttpStatus TS_HTTP_STATUS_RESET_CONTENT

.. c:member:: TSHttpStatus TS_HTTP_STATUS_PARTIAL_CONTENT

.. c:member:: TSHttpStatus TS_HTTP_STATUS_MULTI_STATUS

.. c:member:: TSHttpStatus TS_HTTP_STATUS_ALREADY_REPORTED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_IM_USED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_MULTIPLE_CHOICES

.. c:member:: TSHttpStatus TS_HTTP_STATUS_MOVED_PERMANENTLY

.. c:member:: TSHttpStatus TS_HTTP_STATUS_MOVED_TEMPORARILY

.. c:member:: TSHttpStatus TS_HTTP_STATUS_SEE_OTHER

.. c:member:: TSHttpStatus TS_HTTP_STATUS_NOT_MODIFIED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_USE_PROXY

.. c:member:: TSHttpStatus TS_HTTP_STATUS_TEMPORARY_REDIRECT

.. c:member:: TSHttpStatus TS_HTTP_STATUS_PERMANENT_REDIRECT

.. c:member:: TSHttpStatus TS_HTTP_STATUS_BAD_REQUEST

.. c:member:: TSHttpStatus TS_HTTP_STATUS_UNAUTHORIZED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_PAYMENT_REQUIRED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_FORBIDDEN

.. c:member:: TSHttpStatus TS_HTTP_STATUS_NOT_FOUND

.. c:member:: TSHttpStatus TS_HTTP_STATUS_METHOD_NOT_ALLOWED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_NOT_ACCEPTABLE

.. c:member:: TSHttpStatus TS_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_REQUEST_TIMEOUT

.. c:member:: TSHttpStatus TS_HTTP_STATUS_CONFLICT

.. c:member:: TSHttpStatus TS_HTTP_STATUS_GONE

.. c:member:: TSHttpStatus TS_HTTP_STATUS_LENGTH_REQUIRED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_PRECONDITION_FAILED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE

.. c:member:: TSHttpStatus TS_HTTP_STATUS_REQUEST_URI_TOO_LONG

.. c:member:: TSHttpStatus TS_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE

.. c:member:: TSHttpStatus TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE

.. c:member:: TSHttpStatus TS_HTTP_STATUS_EXPECTATION_FAILED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_UNPROCESSABLE_ENTITY

.. c:member:: TSHttpStatus TS_HTTP_STATUS_LOCKED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_FAILED_DEPENDENCY

.. c:member:: TSHttpStatus TS_HTTP_STATUS_UPGRADE_REQUIRED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_PRECONDITION_REQUIRED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_TOO_MANY_REQUESTS

.. c:member:: TSHttpStatus TS_HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE

.. c:member:: TSHttpStatus TS_HTTP_STATUS_INTERNAL_SERVER_ERROR

.. c:member:: TSHttpStatus TS_HTTP_STATUS_NOT_IMPLEMENTED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_BAD_GATEWAY

.. c:member:: TSHttpStatus TS_HTTP_STATUS_SERVICE_UNAVAILABLE

.. c:member:: TSHttpStatus TS_HTTP_STATUS_GATEWAY_TIMEOUT

.. c:member:: TSHttpStatus TS_HTTP_STATUS_HTTPVER_NOT_SUPPORTED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_VARIANT_ALSO_NEGOTIATES

.. c:member:: TSHttpStatus TS_HTTP_STATUS_INSUFFICIENT_STORAGE

.. c:member:: TSHttpStatus TS_HTTP_STATUS_LOOP_DETECTED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_NOT_EXTENDED

.. c:member:: TSHttpStatus TS_HTTP_STATUS_NETWORK_AUTHENTICATION_REQUIRED

Description
===========
