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

TSHttpHdrUrlGet
***************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSHttpHdrUrlGet(TSMBuffer bufp, TSMLoc offset, TSMLoc * locp)

Description
===========

Get a URL object from an HTTP header. :arg:`bufp` and :arg:`offset` must have been retrieved from
a header object previously (such as via :func:`TSHttpTxnClientReqGet`). :arg:`locp` is updated
on success to refer to the URL object. Note this is a URL *object*, from which URL related
information can be extracted.

The value placed in :arg:`locp` is stable only for a single callback, as other callbacks can
change the URL object itself (see :func:`TSHttpHdrUrlSet`), not just the data in it. That value is
also valid only if this function return ``TS_SUCCESS``.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSHttpTxnClientReqGet(3ts)`,
:manpage:`TSHttpTxnServerReqGet(3ts)`,
:manpage:`TSHttpTxnServerRespGet(3ts)`,
:manpage:`TSHttpTxnClientRespGet(3ts)`
