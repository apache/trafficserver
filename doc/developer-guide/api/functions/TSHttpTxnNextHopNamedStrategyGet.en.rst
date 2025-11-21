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

.. default-domain:: cpp

TSHttpTxnNextHopNamedStrategyGet
********************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void const* TSHttpTxnNextHopNamedStrategyGet(TSHttpTxn txnp, const char *name)

Description
===========

Gets a pointer to the specified :arg:`name` NextHopSelectionStrategy.
This may be nullptr indicating that no strategy exists with the given name.

This function uses the transaction :arg:`txnp` to get access to the
NextHopStrategyFactory associated with the current configuration.

.. note::

   This returned pointer must not be freed and the contents must not
   be changed.
   Strategy pointers held by plugins will become invalid when ATS
   configs are reloaded and should be reset with :func:`TSRemapNewInstance`

See Also
========

:func:`TSHttpTxnNextHopStrategySet`
