.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../../common.defs

.. _developer-plugins-continuations-activate:

Activating Continuations
************************

Continuations are activated when they receive an event or by
``TSContSchedule`` (which schedules a continuation to receive an event).
Continuations might receive an event because:

-  Your plugin calls ``TSContCall``

-  The Traffic Server HTTP state machine sends an event corresponding to
   a particular HTTP hook

-  A Traffic Server IO processor (such as a cache processor or net
   processor) is letting a continuation know there is data (cache or
   network) available to read or write. These callbacks are a result of
   using functions such ``TSVConnRead``/``Write`` or
   ``TSCacheRead``/``Write``


