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

.. doxygen:type:: TSCont

   An opaque type that represents a Traffic Server :term:`continuation`.

.. doxygen:type:: TSEvent

   .. doxygen:description::

.. doxygen:type:: TSEventFunc

   .. doxygen:description::

.. doxygen:type:: TSHostLookupResult

   .. doxygen:description::

.. doxygen:type:: TSHRTime

   "High Resolution Time"

   A 64 bit time value, measured in nanoseconds.

.. doxygen:type:: TSHttpHookID

   An enumeration that identifies a specific type of hook for HTTP transactions.

.. doxygen:type:: TSHttpParser

   .. doxygen:description::

.. doxygen:type:: TSHttpSsn

   An opaque type that represents a Traffic Server :term:`session`.

.. doxygen:type:: TSHttpTxn

   An opaque type that represents a Traffic Server HTTP :term:`transaction`.

.. doxygen:type:: TSIOBuffer

   .. doxygen:description::

.. doxygen:type:: TSIOBufferReader

   .. doxygen:description::

.. doxygen:type:: TSIOBufferSizeIndex

   .. doxygen:description::

.. doxygen:type:: TSLifecycleHookID

   An enumeration that identifies a :ref:`life cycle hook <ts-lifecycle-hook-add>`.

.. doxygen:type:: TSMBuffer

   .. doxygen:description::

.. doxygen:type:: TSMgmtFloat

   The type used internally for a floating point value. This corresponds to the value :const:`TS_RECORDDATATYPE_FLOAT` for
   :type:`TSRecordDataType`.

.. doxygen:type:: TSMgmtInt

   The type used internally for an integer. This corresponds to the value :const:`TS_RECORDDATATYPE_INT` for
   :type:`TSRecordDataType`.

.. doxygen:type:: TSMLoc

   .. doxygen:description::

.. doxygen:type:: TSMutex

   .. doxygen:description::

.. doxygen:type:: TSParseResult

   .. doxygen:description::

.. doxygen:type:: TSPluginRegistrationInfo

   .. doxygen:description::

.. doxygen:type:: TSRecordDataType

   An enumeration that specifies the type of a value in an internal data structure that is accessible via the API.

.. doxygen:type:: TSRemapInterface

   .. doxygen:description::

.. doxygen:type:: TSRemapRequestInfo

   .. doxygen:description::

.. doxygen:type:: TSRemapStatus

   .. doxygen:description::

.. doxygen:type:: TSReturnCode

   An indicator of the results of an API call. A value of :const:`TS_SUCCESS` means the call was successful. Any other value
   indicates a failure and is specific to the API call.

.. doxygen:type:: TSSDKVersion

   .. doxygen:description::

.. doxygen:type:: TSServerState

   .. doxygen:description::

.. doxygen:type:: TSTextLogObject

   .. doxygen:description::

.. doxygen:type:: TSVConn

   .. doxygen:description::

.. doxygen:type:: TSVIO

   .. doxygen:description::
