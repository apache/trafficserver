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

TSMimeHdrFieldValueStringSet
****************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSMimeHdrFieldValueStringSet(TSMBuffer bufp, TSMLoc hdr, TSMLoc field, int idx, const char * value, int length)

Description
===========

:func:`TSMimeHdrFieldValueStringSet` sets the value of a MIME field. The field is identified by the
combination of :arg:`bufp`, :arg:`hdr`, and :arg:`field` which should match those passed to the
function that returned :arg:`field` such as :func:`TSMimeHdrFieldFind`. The :arg:`value` is copied
to the header represented by :arg:`bufp`. :arg:`value` does not have to be null terminated (and in
general should not be).

If :arg:`idx` is non-negative the existing value in the field is treated as a multi-value and
:arg:`idx` as the 0 based index of which element to set. For example if the field had the value
``dave, grigor, tamara`` and :func:`TSMimeHdrFieldValueStringSet` was called with :arg:`value` of
``syeda`` and :arg:`idx` of 1, the value would be set to ``dave, syeda, tamara``. If :arg:`idx` is
non-negative it must be the index of an existing element or exactly one past the last element or the
call will fail. In the example case :arg:`idx` must be between ``0`` and ``3`` inclusive.
:func:`TSMimeHdrFieldValuesCount` can be used to get the current number of elements.

This function returns :macro:`TS_SUCCESS` if the value was set, :macro:`TS_ERROR` if not.
