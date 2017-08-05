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

TSMimeHdrFieldCreate
********************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSMimeHdrFieldCreate(TSMBuffer bufp, TSMLoc hdr, TSMLoc * out)
.. function:: TSReturnCode TSMimeHdrFieldCreateNamed(TSMBuffer bufp, TSMLoc hdr, const char * name, int name_len, TSMLoc * out)

Description
===========

These functions create MIME fields in a MIME header. The header is specified by the combination of
the buffer :arg:`bufp` and a location :arg:`hdr`. The header must be either created such as by
:func:`TSMimeHdrCreate` or be an existing header found via a function such as :func:`TSHttpTxnClientReqGet`.

:func:`TSMimeHdrFieldCreate` creates a completely empty field which must be named before being used
in a header, usually via :func:`TSMimeHdrFieldNameSet`. It is almost always more convenient to use
:func:`TSMimeHdrFieldCreateNamed` which combines these two steps, creating the field and then
setting the name to :arg:`name`.

For both functions a reference to the new field is returned via arg:`out`.

The field created is not in a header even though it is in the same buffer. It can be added to a
header with :func:`TSMimeHdrFieldAppend`. The field also has no value, only a name. If a value is
needed it must be added explicitly with a function such as :func:`TSMimeHdrFieldValueIntSet`.
