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

TSLifecycleHookID
*****************

Synopsis
========

`#include <ts/apidefs.h>`

.. c:type:: TSLifeCycleHookID

Enum typedef used to indicate the event hook being called during a continuation
invocation.

Enumeration Members
===================

.. c:member:: TSLifecycleHookID TS_LIFECYCLE_PORTS_INITIALIZED_HOOK

.. c:member:: TSLifecycleHookID TS_LIFECYCLE_PORTS_READY_HOOK

.. c:member:: TSLifecycleHookID TS_LIFECYCLE_CACHE_READY_HOOK

.. c:member:: TSLifecycleHookID TS_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED_HOOK

.. c:member:: TSLifecycleHookID TS_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED_HOOK

.. c:member:: TSLifecycleHookID TS_LIFECYCLE_LAST_HOOK

Description
===========

