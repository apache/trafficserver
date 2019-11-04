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

TSHttpHdrHostGet
****************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: const char* TSHttpHdrHostGet(TSMBuffer bufp, TSMLoc offset, int * length)

Description
===========

Get the host for the request. :arg:`bufp` and :arg:`offset` must reference an
HTTP request header. A pointer to the host is returned and the length is stored
in the ``int`` pointed at by :arg:`length`. Note the returned text may not
be null terminated. The URL in the request is checked first then the ``Host``
header field.

.. note::

   This is much faster than calling :c:func:`TSHttpTxnEffectiveUrlStringGet` and
   extracting the host from the result.
