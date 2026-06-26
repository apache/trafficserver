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

TSMimeHdrFieldNameSet
*********************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSMimeHdrFieldNameSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, const char * name, int length)

Description
===========

:func:`TSMimeHdrFieldNameSet` sets the name of the MIME field identified by :arg:`bufp`,
:arg:`hdr`, and :arg:`field` to :arg:`name`. The :arg:`name` is copied into the header
represented by :arg:`bufp` and does not have to be null terminated. :arg:`length` is the length
of :arg:`name`, or ``-1`` if :arg:`name` is null terminated.

A header field name is stored with a 16-bit length, so it is limited to ``65535`` (``UINT16_MAX``)
bytes. A :arg:`name` longer than that is rejected: the field's existing name is left unchanged and
the function returns :enumerator:`TS_ERROR`.

This function returns :enumerator:`TS_SUCCESS` if the name was set, :enumerator:`TS_ERROR` if not.
