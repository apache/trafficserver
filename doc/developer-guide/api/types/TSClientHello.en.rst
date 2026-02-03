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

TSClientHello
*************

Synopsis
========

.. code-block:: cpp

    #include <ts/apidefs.h>

.. type:: TSClientHello

.. type:: TSClientHelloImpl::TSExtensionTypeList

   A type alias for an iterable container of extension type IDs.


Description
===========

:type:`TSClientHello` is an opaque handle to a TLS ClientHello message sent by
a client during the TLS handshake. It provides access to the client's TLS
version, cipher suites, and extensions.

Objects of this type are obtained via :func:`TSVConnClientHelloGet` and must
be freed using :func:`TSClientHelloDestroy`. The implementation abstracts
differences between OpenSSL and BoringSSL to provide a consistent interface.

Accessor Methods
================

The following methods are available to access ClientHello data:

.. function:: uint16_t get_version() const

   Returns the TLS version from the ClientHello message.

.. function:: const uint8_t* get_cipher_suites() const

   Returns a pointer to the cipher suites buffer. The length is available via
   :func:`get_cipher_suites_len()`.

.. function:: size_t get_cipher_suites_len() const

   Returns the length of the cipher suites buffer in bytes.

.. function:: const uint8_t* get_extensions() const

   Returns a pointer to the extensions buffer (BoringSSL format). The length is
   available via :func:`get_extensions_len()`. May return ``nullptr`` if using
   OpenSSL.

.. function:: size_t get_extensions_len() const

   Returns the length of the extensions buffer in bytes.

.. function:: const int* get_extension_ids() const

   Returns a pointer to the extension IDs array (OpenSSL format). The length is
   available via :func:`get_extension_ids_len()`. May return ``nullptr`` if using
   BoringSSL.

.. function:: size_t get_extension_ids_len() const

   Returns the number of extension IDs in the array.

.. function:: TSClientHelloImpl::TSExtensionTypeList get_extension_types() const

   Returns an iterable container of extension type IDs present in the ClientHello.
   This method abstracts the differences between BoringSSL (which uses an extensions
   buffer) and OpenSSL (which uses an extension_ids array), providing a consistent
   interface regardless of the SSL library in use.

.. function:: void* get_ssl_ptr() const

   Returns the underlying SSL pointer. This is an internal accessor for advanced use
   cases.
