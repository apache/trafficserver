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

TSHttpTxnResponseActionSet
**************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void TSHttpTxnResponseActionSet(TSHttpTxn txnp, TSResponseAction *action)

Description
===========

Takes a ResponseAction and sets it as the behavior for finding the
next parent.

Be aware ATS will never change this outside a plugin.
Therefore, plugins which set the ResponseAction to retry
must also un-set it after the subsequent success or failure,
or ATS will retry forever!

The passed *action* must not be null, and is copied and may be
destroyed after this call returns.

Callers must maintain owernship of action.hostname,
and its lifetime must exceed the transaction.
