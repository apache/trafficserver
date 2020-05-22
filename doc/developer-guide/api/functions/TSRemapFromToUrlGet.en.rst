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

TSRemapFrom/ToUrlGet
********************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSRemapFromUrlGet(TSHttpTxn txnp, TSMLoc * urlLocp)
.. function:: TSReturnCode TSRemapToUrlGet(TSHttpTxn txnp, TSMLoc * urlLocp)

Description
===========

These functions are useful for transactions where the URL is remapped, due to matching a line in :file:`remap.config`.
:func:`TSRemapFromUrlGet` returns the *from* URL in the matching line in :file:`remap.config`.
:func:`TSRemapToUrlGet` returns the *to* URL in the matching line in :file:`remap.config`.
This info is available at or after the :c:data:`TS_HTTP_POST_REMAP_HOOK` hook.  If the function returns
:data:`TS_SUCCESS`, the location of the URL is put into the variable pointed to by :arg:`urlLocp`.  On error, the function
returns :data:`TS_ERROR`.
