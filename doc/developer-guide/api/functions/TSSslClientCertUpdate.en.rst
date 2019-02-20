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

TSSslClientCertUpdate
************************

Traffic Server TLS client cert update

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSSslClientCertUpdate(const char *cert_path, const char *key_path)

Description
===========
:func:`TSSslClientCertUpdate` updates existing client certificates configured in :file:`ssl_server_name.yaml` or `proxy.config.ssl.client.cert.filename`. :arg:`cert_path` should be exact match as provided in configurations.
:func:`TSSslClientCertUpdate` returns `TS_SUCCESS` only if :arg:`cert_path` exists in configuration and reloaded to update the context.

Type
====
.. type:: TSReturnCode
