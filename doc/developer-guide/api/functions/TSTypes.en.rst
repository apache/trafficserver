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

.. include:: ../../../common.defs

.. Many types are here simply to avoid build errors in the documentation. It is reasonable to,
   when providing additional documentation on the type, to move it from here to a more appropriate
   file.

.. default-domain:: c

TSAPI Types
***********

Synopsis
========

`#include <ts/ts.h>`
`#include <ts/remap.h>`

Description
===========

The Apache Traffic Server API provides large number of types. Many of them are
specific to a particular API function or function group, but others are used
more widely. Those are described on this page.

.. type:: ink_hrtime

.. type:: INK_MD5

   Buffer type sufficient to contain an MD5 hash value.

.. cpp:class:: INK_MD5

   See :type:`INK_MD5`.

.. cpp:class:: RecRawStatBlock

   A data block intended to contain |TS| statistics.

.. type:: TSAction

.. type:: TSCacheKey

.. type:: TSConfig

.. type:: TSConfigDestroyFunc

.. type:: TSCont

   An opaque type that represents a Traffic Server :term:`continuation`.

.. type:: TSEventFunc

.. type:: TSFile

.. type:: TSHostLookupResult

   A type representing the result of a call to :func:`TSHostLookup`. Use with :func:`TSHostLookupResultAddrGet`.

.. type:: TSHRTime

   "High Resolution Time"

   A 64 bit time value, measured in nanoseconds.

.. type:: TSHttpParser

.. type:: TSHttpSsn

   An opaque type that represents a Traffic SeUuirver :term:`session`.

.. type:: TSHttpTxn

   An opaque type that represents a Traffic Server HTTP :term:`transaction`.

.. type:: TSIOBuffer

.. type:: TSIOBufferBlock

.. type:: TSIOBufferReader

.. type:: TSIOBufferSizeIndex

.. type:: TSLifecycleHookID

   An enumeration that identifies a :ref:`life cycle hook <ts-lifecycle-hook-add>`.

.. type:: TSMBuffer

.. type:: TSMgmtCounter

.. type:: TSMgmtFloat

   The type used internally for a floating point value. This corresponds to the value :const:`TS_RECORDDATATYPE_FLOAT` for
   :type:`TSRecordDataType`.

.. type:: TSMgmtInt

   The type used internally for an integer. This corresponds to the value :const:`TS_RECORDDATATYPE_INT` for
   :type:`TSRecordDataType`.

.. type:: TSMgmtString

.. type:: TSMimeParser

.. type:: TSMLoc

.. var:: TSMLoc TS_NULL_MLOC

   A predefined null valued :type:`TSMLoc` used to indicate the absence of an :type:`TSMLoc`.

.. type:: TSMutex

.. type:: TSPluginRegistrationInfo

   The following struct is used by :func:`TSPluginRegister`.

   It stores registration information about the plugin.

.. type:: TSRemapInterface

.. type:: TSRemapRequestInfo

.. type:: TSSslX509

    This type represents the :code:`X509` object created from an SSL certificate.

.. type:: TSTextLogObject

   This type represents a custom log file that you create with
   :func:`TSTextLogObjectCreate`.

   Your plugin writes entries into this log file using
   :func:`TSTextLogObjectWrite`.

.. type:: TSThread

.. type:: TSThreadFunc

.. type:: TSUuidVersion

   A version value for at :type:`TSUuid`.

   .. member:: TS_UUID_V4

      A version 4 UUID. Currently only this value is used.

.. var:: size_t TS_UUID_STRING_LEN

   Length of a UUID string.

.. type:: TSVConn

    A virtual connection. This is the basic mechanism for abstracting I/O operations in |TS|.

.. type:: TSNetVConnection

    A subtype of :type:`TSVConn` that provides additional IP network information and operations.

.. type:: TSVIO

.. type:: ModuleVersion

    A module version.

.. cpp:type:: ModuleVersion

    A module version.

.. cpp:class:: template<typename T> DLL

    An anchor for a double linked instrusive list of instance of :arg:`T`.

.. cpp:class:: template<typename T> Queue

.. type:: TSAcceptor

.. type:: TSNextProtocolSet

.. cpp:class:: template <typename T> LINK

.. cpp:class:: VersionNumber

   A two part version number, defined in :ts:git:`include/tscore/I_Version.h`.

   .. cpp:member:: short int ink_major

      Major version number.

   .. cpp:member:: short int ink_minor

      Minor version number.
