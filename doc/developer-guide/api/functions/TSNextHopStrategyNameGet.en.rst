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

TSNextHopStrategyNameGet
************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: char const* TSNextHopStrategyNameGet(TSStrategy strategy)

Description
===========

Gets the name of :arg:`strategy`. Returns nullptr if :arg:`strategy` is
nullptr, i.e. no strategy is set and parent.config is in use.

.. note::

   This returned pointer must not be freed and the contents must not
   be changed.
   Strategy pointers held by plugins become invalid when ATS configs
   are reloaded and must be re-obtained during :func:`TSRemapNewInstance`

Deprecated
==========

.. function:: char const *TSHttpNextHopStrategyNameGet(void const *strategy)

   .. deprecated:: 11.0.0
      Use :func:`TSNextHopStrategyNameGet`. This 10.2 name will be
      removed in a future major release.

   Forwards to :func:`TSNextHopStrategyNameGet`.

See Also
========

:func:`TSHttpTxnNextHopStrategyGet`
