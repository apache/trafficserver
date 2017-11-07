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

TSVConnGetUserData
*************

Synopsis
========

`#include <ts/ts.h>`

.. function:: void* TSVConnGetUserData(TSVConn connp, const char* name)

Description
===========

:func: `TSVConnGetUserData` returns the pointer stored in TSVConn which was set using
the call to :func: `TSVConnSetUserData`. Returns NULL if the supplied name does not match
any of the stored entries.

See also
========

:manpage:`TSVConnSetUserData(3ts)`, :manpage:`TSAPI(3ts)`
