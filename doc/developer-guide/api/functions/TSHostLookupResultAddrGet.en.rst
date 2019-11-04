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

TSHostLookupResultAddrGet
*************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: sockaddr const* TSHostLookupResultAddrGet(TSHostLookupResult lookup_result)

Description
===========

Retrieves the pointer to a ``sockaddr`` of a the given :arg:`lookup_result` from a previous call to :func:`TSHostLookup`.

For example:

.. code-block:: c

    int
    handler(TSCont contp, TSEvent event, void *edata) {
        if (event == TS_EVENT_HOST_LOOKUP) {
            const sockaddr *addr = TSHostLookupResultAddrGet(static_cast<TSHostLookupResult>(edata));
            // TODO Add logic here.
        }
    }
