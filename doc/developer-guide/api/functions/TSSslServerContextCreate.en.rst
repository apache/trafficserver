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

TSSslServerContextCreate
************************

Traffic Server TLS server context creation.

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSSslContext TSSslServerContextCreate(TSSslX509 *cert, char *certname)
.. function:: void TSSslContextDestroy(TSSslContext ctx)

Description
===========

:func:`TSSslServerContextCreate` creates a new TLS server context. The context
is configured using the TLS settings specified in :file:`records.config`. The user can pass certificate object(:type:`TSSslX509` :arg:`cert`
and certname (:code:`const char*` :arg:`certname`) optionally.
This function sets the certificate status callback and initializes OCSP stapling data if :arg:`cert` and :arg:`certname` is provided and ocsp is enabled globally.
:func:`TSSslServerContextCreate` returns ``nullptr`` on failure.

:func:`TSSslContextDestroy` destroys a TLS context created by
:func:`TSSslServerContextCreate`. If :arg:`ctx` is ``nullptr`` no operation is
performed.

Type
====

.. type:: TSSslContext

	The SSL context object. This is an opaque type that can be cast to
	the underlying SSL library type (:code:`SSL_CTX *` for the OpenSSL library).

See also
========

:manpage:`TSAPI(3ts)`
