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

.. include:: ../../../../common.defs

Setting a Global Hook
*********************

Global hooks are always added in ``TSPluginInit`` using
``TSHttpHookAdd``. The two arguments of ``TSHttpHookAdd`` are the hook
ID and the continuation to call when processing the event corresponding
to the hook. In ``blacklist_1.c``, the global hook is added as follows:

.. code-block:: c

   TSHttpHookAdd (TS_HTTP_OS_DNS_HOOK, contp);

Above, ``TS_HTTP_OS_DNS_HOOK`` is the ID for the origin server DNS
lookup hook and ``contp`` is the parent continuation created earlier.

This means that the Blacklist plugin is called at every origin server
DNS lookup. When it is called, the handler functio ``blacklist_plugin``
receives ``TS_EVENT_HTTP_OS_DNS`` and calls ``handle_dns`` to see if the
request is forbidden.
