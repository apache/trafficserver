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

TSMimeHdrFieldAppend
********************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSMimeHdrFieldAppend(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)

Description
===========

Attatches a MIME :arg:`field` to a header. The header is represented by the :arg:`bufp` and :arg:`hdr`
arguments which should have been obtained by a call to :func:`TSHttpTxnClientReqGet` or similar. If
the field in :arg:`field` was created by calling :func:`TSMimeHdrFieldCreateNamed` the same
:arg:`bufp` and :arg:`hdr` passed to that should be passed to this function.

Returns :code:`TS_SUCCESS` if the :arg:`field` was attached to the header, :code:`TS_ERROR` if it
was not. Fields cannot be attached to read only headers.
