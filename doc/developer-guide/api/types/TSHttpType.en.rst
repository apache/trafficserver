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

TSHttpType
**********

Synopsis
========

.. code-block:: cpp

    #include <ts/apidefs.h>

.. c:type:: TSHttpType

Enum typedef which defines the possible HTTP types assigned to an HTTP header,
as returned by :c:func:`TSHttpHdrTypeGet`. Headers created by
:c:func:`TSHttpHdrCreate` receive :c:member:`TS_HTTP_TYPE_UNKNOWN` by default
and may be modified once by using :c:func:`TSHttpHdrTypeSet`.

Enumeration Members
===================

.. c:member:: TSHttpType TS_HTTP_TYPE_UNKNOWN

Default for new headers created by :c:func:`TSHttpHdrCreate`.

.. c:member:: TSHttpType TS_HTTP_TYPE_REQUEST

HTTP request headers.

.. c:member:: TSHttpType TS_HTTP_TYPE_RESPONSE

HTTP response headers.

Description
===========

