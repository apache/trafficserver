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

TSRemapNextHopStrategySet
*************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void TSRemapNextHopStrategySet(TSStrategy strategy)

Description
===========

Sets the next hop strategy for the currently loading remap rule.
Pass nullptr to clear the rule's strategy so that parent.config will
be used instead.

The :arg:`strategy` pointer must have been obtained from
:func:`TSRemapNextHopStrategyFind` during the current remap
configuration load. A handle that is not present in the loading
NextHopStrategyFactory (e.g. one cached across a configuration reload)
is normally rejected: the call is logged and has no effect. As with
:func:`TSHttpTxnNextHopStrategySet`, the check compares addresses and is
a safety net rather than a guarantee. Only membership in the loading
factory is checked, so a handle obtained while loading a different rule
of the same configuration is accepted.

This function may ONLY be called during TSRemapNewInstance. A call made
outside of remap rule initialization has no effect and is logged to
``diags.log`` the first time it occurs.

.. note::

   This strategy pointer must not be freed and the contents must not
   be changed.
   Strategy pointers held by plugins become invalid when ATS configs
   are reloaded and must be re-obtained during :func:`TSRemapNewInstance`

See Also
========

:func:`TSRemapNextHopStrategyGet`, :func:`TSRemapNextHopStrategyFind`.
