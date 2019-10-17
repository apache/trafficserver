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

TSSslServerCertUpdate
************************

Traffic Server TLS server cert update

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSSslServerCertUpdate(const char *cert_path, const char *key_path)

Description
===========

:func:`TSSslServerCertUpdate` updates existing server certificates configured in
:file:`ssl_multicert.config` based on the common name in :arg:`cert_path`. if :arg:`key_path` is set
to nullptr, the function will use :arg:`cert_path` for both certificate and private key.
:func:`TSSslServerCertUpdate` returns `TS_SUCCESS` only if there exists such a mapping,
:arg:`cert_path` is a valid cert, and the context is updated to use that cert.
