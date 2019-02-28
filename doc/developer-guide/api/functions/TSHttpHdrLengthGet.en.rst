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

TSHttpHdrLengthGet
******************

Synopsis
========

`#include <ts/ts.h>`

.. function:: int TSHttpHdrLengthGet(TSMBuffer bufp, TSMLoc mloc)

Description
===========

Return the length in characters of the HTTP header specified by :arg:`bufp` and :arg:`mloc` which
must specify a valid HTTP header. Usually these values would have been obtained via an earlier call
to
:func:`TSHttpTxnServerReqGet`,
:func:`TSHttpTxnClientReqGet`,
:func:`TSHttpTxnServerRespGet`,
:func:`TSHttpTxnClientRespGet`,
or via calls to create a new HTTP header such as :func:`TSHttpHdrCreate`.
