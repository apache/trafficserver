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

TSPluginVCIOBufferIndexGet
**************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSIOBufferSizeIndex TSPluginVCIOBufferIndexGet(TSHttpTxn txnp)

Description
===========

Convenience function to obtain the current value of the
:ts:cv:`proxy.config.plugin.vc.default_buffer_index` parameter from the
transaction provided in :arg:`txnp`. If no errors are encountered and the
buffer index on the transaction is greater than or equal to the minimum value,
and less than or equal to the maximum value, this value is returned. Otherwise
the default buffer index is returned.
