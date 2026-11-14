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

TSHttpTxnNextHopStrategyFind
****************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSStrategy TSHttpTxnNextHopStrategyFind(TSHttpTxn txnp, const char *name)

Description
===========

Gets a pointer to the specified :arg:`name` NextHopSelectionStrategy.
This may be nullptr indicating that no strategy exists with the given
name, or that no rewrite table or strategy factory existed when the
transaction started.

This function uses the transaction :arg:`txnp` to get access to the
NextHopStrategyFactory associated with the current configuration.

.. note::

   :arg:`name` must not be ``nullptr``. Passing a null name is an API
   violation and will abort the process in default builds.

.. note::

   This returned pointer must not be freed and the contents must not
   be changed.
   Strategy pointers held by plugins become invalid when ATS configs
   are reloaded and must be re-obtained during :func:`TSRemapNewInstance`

Deprecated
==========

.. function:: void const *TSHttpTxnNextHopNamedStrategyGet(TSHttpTxn txnp, const char *name)

   .. deprecated:: 11.0.0
      Use :func:`TSHttpTxnNextHopStrategyFind`. This 10.2 name will be
      removed in a future major release.

   Forwards to :func:`TSHttpTxnNextHopStrategyFind`.

See Also
========

:func:`TSHttpTxnNextHopStrategySet`
