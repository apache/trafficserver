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

.. code-block:: cpp

    #include <ts/ts.h>
    #include <ts/remap.h>

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

   An opaque type that represents a Traffic Server :term:`session`.

.. type:: TSHttpTxn

   An opaque type that represents a Traffic Server HTTP :term:`transaction`.

.. type:: TSIOBuffer

.. type:: TSIOBufferBlock

.. type:: TSIOBufferReader

.. type:: TSIOBufferSizeIndex

.. type:: TSLifecycleHookID

   An enumeration that identifies a :ref:`life cycle hook <ts-lifecycle-hook-add>`.

.. type:: TSMBuffer

   Internally, data for a transaction is stored in one or more :term:`header heap`\s. These are
   storage local to the transaction, and generally each HTTP header is stored in a separate one.
   This type is a handle to a header heap, and is provided or required by functions that locate HTTP
   header related data.

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

   This is a memory location relative to a :term:`header heap` represented by a :c:type:`TSMBuffer` and
   must always be used in conjunction with that :c:type:`TSMBuffer` instance. It identifies a specific
   object in the :c:type:`TSMBuffer`. This indirection is needed so that the :c:type:`TSMBuffer`
   can reallocate space as needed. Therefore a raw address obtained from a :c:type:`TSMLoc` should
   be considered volatile that may become invalid across any API call.

.. var:: TSMLoc TS_NULL_MLOC

   A predefined null valued :type:`TSMLoc` used to indicate the absence of an :type:`TSMLoc`.

.. type:: TSMutex

.. type:: TSPluginRegistrationInfo

   The following struct is used by :func:`TSPluginRegister`.

   It stores registration information about the plugin.

.. type:: TSRemapInterface

   Data passed to a remap plugin via :func:`TSRemapInit`.

   .. member:: unsigned long size

      The size of the structure in bytes, including this member.

   .. member:: unsigned long tsremap_version

      The API version of the C API. The lower 16 bits are the minor version, and the upper bits
      the major version.

.. type:: TSRemapRequestInfo

   Data passed to a remap plugin during the invocation of a remap rule.

   .. member:: TSMBuffer requestBufp

      The client request. All of the other :type:`TSMLoc` values use this as the base buffer.

   .. member:: TSMLoc requestHdrp

      The client request.

   .. member:: TSMLoc mapFromUrl

      The match URL in the remap rule.

   .. member:: TSMLoc mapToUrl

      The target URL in the remap rule.

   .. member:: TSMLoc requestUrl

      The current request URL. The remap rule and plugins listed earlier in the remap rule can modify this
      from the client request URL. Remap plugins are expected to modify this value to perform the
      remapping of the request. Note this is the same :code:`TSMLoc` as would be obtained by
      calling :func:`TSHttpTxnClientReqGet`.

   .. member:: int redirect

      Flag for using the remapped URL as an explicit redirection. This can be set by the remap plugin.

.. type:: TSSslX509

    This type represents the :code:`X509` object created from an SSL certificate.

.. type:: TSTextLogObject

   This type represents a custom log file that you create with
   :func:`TSTextLogObjectCreate`.

   Your plugin writes entries into this log file using
   :func:`TSTextLogObjectWrite`.

.. type:: TSThread

      This represents an internal |TS| thread, created by the |TS| core. It is an opaque type which
      can be used only to check for equality / inequality, and passed to API functions. An instance
      that refers to the current thread can be obtained with :func:`TSThreadSelf`.

.. type:: TSEventThread

      This type represents an :term:`event thread`. It is an opaque which is used to specify a
      particular event processing thread in |TS|. If plugin code is executing in an event thread
      (which will be true if called from a hook or a scheduled event) then the current event thread
      can be obtained via :func:`TSEventThreadSelf`.

      A :code:`TSEventThread` is also a :type:`TSThread` and can be passed as an argument to any
      parameter of type :type:`TSThread`.

.. type:: TSThreadFunc

.. type:: TSUserArgType

   An enum for the supported types of user arguments.

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

    An anchor for a double linked intrusive list of instance of :arg:`T`.

.. cpp:class:: template<typename T> Queue

.. type:: TSAcceptor

.. cpp:class:: template <typename T> LINK

.. cpp:class:: VersionNumber

   A two part version number, defined in :ts:git:`include/tscore/I_Version.h`.

   .. cpp:member:: short int ink_major

      Major version number.

   .. cpp:member:: short int ink_minor

      Minor version number.

.. type:: TSFetchUrlParams_t
.. type:: TSFetchSM
.. type:: TSFetchEvent

.. type:: TSHttpPriority

   The abstract type of the various HTTP priority implementations.

   .. member:: uint8_t priority_type

      The reference to the concrete HTTP priority implementation. This will be
      a value from TSHttpPriorityType

   .. member:: uint8_t data[7]

      The space allocated for the concrete priority implementation.

      Note that this has to take padding into account. There is a static_assert
      in ``InkAPI.cc`` to verify that :type:`TSHttpPriority` is at least as large as
      :type:`TSHttp2Priority`. As other structures are added that are represented by
      :type:`TSHttpPriority` add more static_asserts to verify that :type:`TSHttpPriority` is as
      large as it needs to be.


.. type:: TSHttp2Priority

   A structure for HTTP/2 priority. For an explanation of these terms with respect
   to HTTP/2, see RFC 7540, section 5.3.

   .. member:: uint8_t priority_type

      HTTP_PROTOCOL_TYPE_HTTP_2

   .. member:: uint8_t weight

   .. member:: int32_t stream_dependency

      The stream dependency. Per spec, see RFC 7540 section 6.2, this is 31
      bits. We use a signed 32 bit structure to store either a valid dependency
      or -1 if the stream has no dependency.
