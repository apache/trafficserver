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

TSHttpCntlType
**************

Synopsis
========

.. code-block:: cpp

    #include <ts/apidefs.h>

.. c:enum:: TSHttpCntlType

   The feature to control.

   .. c:enumerator:: TS_HTTP_CNTL_LOGGING_MODE

      Turn off (or on) all logging for this transaction.

   .. c:enumerator:: TS_HTTP_CNTL_INTERCEPT_RETRY_MODE

      Control the intercept retry mode.

   .. c:enumerator:: TS_HTTP_CNTL_RESPONSE_CACHEABLE

      Make the response cacheable or uncacheable.

   .. c:enumerator:: TS_HTTP_CNTL_REQUEST_CACHEABLE

      Make the request cacheable or uncacheable.

   .. c:enumerator:: TS_HTTP_CNTL_SERVER_NO_STORE

      Make the server response uncacheable.

   .. c:enumerator:: TS_HTTP_CNTL_TXN_DEBUG

      Turn on transaction debugging.

   .. c:enumerator:: TS_HTTP_CNTL_SKIP_REMAPPING

      Skip the remap requirement for this transaction.

Description
===========

These enumeration members are used together with the APIs to Set and Get
control status of a transaction.
