.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. default-domain:: c

===========
TSAPI Types
===========

Synopsis
========
| `#include <ts/ts.h>`
| `#include <ts/remap.h>`

Description
===========

The Apache Traffic Server API provides large number of types. Many of them are specific to a particular API function or
function group, but others are used more widely. Those are described on this page.

.. type:: TSCont

   An opaque type that represents a Traffic Server :term:`continuation`.

.. type:: TSEvent

   :type:`TSEvents` are sent to continuations when they are called
   back.

   The :type:`TSEvent` provides the continuation's handler function
   with information about the callback.  Based on the event it
   receives, the handler function can decide what to do.

.. type:: TSEventFunc

.. type:: TSHostLookupResult

.. type:: TSHRTime

   "High Resolution Time"

   A 64 bit time value, measured in nanoseconds.

.. type:: TSHttpHookID

   An enumeration that identifies a specific type of hook for HTTP transactions.

.. type:: TSHttpParser

.. type:: TSHttpSsn

   An opaque type that represents a Traffic Server :term:`session`.

.. type:: TSHttpTxn

   An opaque type that represents a Traffic Server HTTP :term:`transaction`.

.. type:: TSIOBuffer

.. type:: TSIOBufferReader

.. type:: TSIOBufferSizeIndex

.. type:: TSLifecycleHookID

   An enumeration that identifies a :ref:`life cycle hook <ts-lifecycle-hook-add>`.

.. type:: TSMBuffer

.. type:: TSMgmtFloat

   The type used internally for a floating point value. This corresponds to the value :const:`TS_RECORDDATATYPE_FLOAT` for
   :type:`TSRecordDataType`.

.. type:: TSMgmtInt

   The type used internally for an integer. This corresponds to the value :const:`TS_RECORDDATATYPE_INT` for
   :type:`TSRecordDataType`.

.. type:: TSMLoc

.. type:: TSMutex

.. type:: TSParseResult

   This set of enums are possible values returned by
   :func:`TSHttpHdrParseReq` and :func:`TSHttpHdrParseResp`.

.. type:: TSPluginRegistrationInfo

   The following struct is used by :func:`TSPluginRegister`.

   It stores registration information about the plugin.

.. type:: TSRecordDataType

   An enumeration that specifies the type of a value in an internal data structure that is accessible via the API.

.. type:: TSRemapInterface

.. type:: TSRemapRequestInfo

.. type:: TSRemapStatus

.. type:: TSReturnCode

   An indicator of the results of an API call. A value of :const:`TS_SUCCESS` means the call was successful. Any other value
   indicates a failure and is specific to the API call.

.. type:: TSSDKVersion

   Starting 2.0, SDK now follows same versioning as Traffic Server.

.. type:: TSServerState

.. type:: TSTextLogObject

   This type represents a custom log file that you create with
   :func:`TSTextLogObjectCreate`.

   Your plugin writes entries into this log file using
   :func:`TSTextLogObjectWrite`.

.. type:: TSVConn

.. type:: TSVIO
