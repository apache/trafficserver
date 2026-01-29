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


Description
===========

:type:`TSClientHello` is an opaque handle to a TLS ClientHello message sent by
a client during the TLS handshake. It provides access to the client's TLS
version, cipher suites, and extensions.

Objects of this type are obtained via :func:`TSVConnClientHelloGet` and must
be freed using :func:`TSClientHelloDestroy`. The implementation abstracts
differences between OpenSSL and BoringSSL to provide a consistent interface.
