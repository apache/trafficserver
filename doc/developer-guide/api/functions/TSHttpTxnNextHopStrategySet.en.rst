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

TSHttpTxnNextHopStrategySet
***************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void TSHttpTxnNextHopStrategySet(TSHttpTxn txnp, TSStrategy strategy)

Description
===========

Sets the next hop strategy for the transaction :arg:`txnp`.
This :arg:`strategy` pointer must be a live strategy in the current
configuration's NextHopStrategyFactory, or nullptr to indicate that
parent.config will be used instead.

A non-null handle that is not present in the transaction's strategy
factory (e.g. one cached across a configuration reload) is normally
rejected: the call is logged and has no effect, leaving the
transaction's strategy unchanged. The check compares addresses, so it is
a safety net rather than a guarantee: it never lets the core use a freed
strategy, but if a stale handle's address has been reused by a strategy
of the current configuration, that strategy is used. Plugins must
re-obtain strategy handles after a configuration reload.

An accepted strategy stays valid for the rest of the transaction, even if
remap later selects a different rewrite table (e.g. a virtual host table
or one from a configuration reload): the transaction keeps the
configuration that owns the strategy alive.

Plugins can get the transaction's active strategy with
:func:`TSHttpTxnNextHopStrategyGet`, or look up a strategy by name with
:func:`TSHttpTxnNextHopStrategyFind`, which uses the transaction's
NextHopStrategyFactory strategy database.

.. note::

   This strategy pointer must not be freed and the contents must not
   be changed.
   Strategy pointers held by plugins become invalid when ATS configs
   are reloaded and must be re-obtained during :func:`TSRemapNewInstance`

Deprecated
==========

.. function:: void TSHttpTxnNextHopStrategySet(TSHttpTxn txnp, void const *strategy)

   .. deprecated:: 11.0.0
      Pass a :type:`TSStrategy` instead. This 10.2 signature will be
      removed in a future major release.

   Forwards to the :type:`TSStrategy` overload, so the same check
   applies. A ``nullptr`` argument selects a non-deprecated overload;
   pass ``nullptr`` rather than ``NULL`` or ``0``, which are ambiguous.

See Also
========

:func:`TSHttpTxnNextHopStrategyGet`, :func:`TSHttpTxnNextHopStrategyFind`.
