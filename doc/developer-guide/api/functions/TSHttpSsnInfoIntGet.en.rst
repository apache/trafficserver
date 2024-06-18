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

.. default-domain:: cpp

TSHttpSsnInfoIntGet
===================

Synopsis
--------

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpSsnInfoIntGet(TSHttpSsn ssnp, TSHttpSsnInfoKey key, TSMgmtInt * value, uint64_t subkey = 0)

Description
-----------

:func:`TSHttpSsnInfoIntGet` returns arbitrary integer-typed info about a session as defined in
:cpp:type:`TSHttpSsnInfoKey`. The API will be part of a generic API umbrella that can support returning
arbitrary info about a session using custom log tags.

The :cpp:type:`TSHttpSsnInfoKey` currently supports the below integer-based info about a transaction

.. enum:: TSHttpSsnInfoKey

   .. enumerator:: TS_SSN_INFO_TRANSACTION_COUNT

      The value indicate the number of transactions made on the session.

   .. enumerator:: TS_SSN_INFO_RECEIVED_FRAME_COUNT

      The value indicate the number of HTTP/2 or HTTP/3 frames received on the session.
      A frame type must be specified by passing it to subkey.
      You can use TS_SSN_INFO_RECEIVED_FRAME_COUNT_H2_UNKNOWN and TS_SSN_INFO_RECEIVED_FRAME_COUNT_H3_UNKNOWN to get the value for
      unknown frames.

Return values
-------------

The API returns :enumerator:`TS_SUCCESS`, if the requested info is supported, :enumerator:`TS_ERROR` otherwise.
