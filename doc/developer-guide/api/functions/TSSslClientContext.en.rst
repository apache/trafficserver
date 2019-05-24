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

TSSslClientContext
******************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSSslClientContextsNamesGet(int n, const char **result, int *actual)

.. function:: TSSslContext TSSslClientContextFindByName(const char *ca_paths, const char *ck_paths)
 
Description
===========

These functions are used to explore the client contexts that |TS| uses to connect to upstreams.

:func:`TSSslClientContextsNamesGet` can be used to retrieve the entire client context mappings. Note 
that in traffic server, client contexts are stored in a 2-level mapping with ca paths and cert/key 
paths as keys. Hence every 2 null-terminated string in :arg:`result` can be used to lookup one context.
:arg:`result` points to an user allocated array that will hold pointers to lookup key strings and 
:arg:`n` is the size for :arg:`result` array. :arg:`actual`, if valid, will be filled with actual number
of lookup keys (2 for each context).

:func:`TSSslClientContextFindByName` can be used to retrieve the client context pointed by the lookup 
key pairs. User should call :func:`TSSslClientContextsNamesGet` first to determine which lookup keys are 
present before quering for the context. :arg:`ca_paths` should be the first key and :arg:`ck_paths` 
should be the second. This function returns NULL if the client context mapping are changed and no valid 
context exists for the key pair. The caller is responsible for releasing the context returned by this 
function with :func:`TSSslContextDestroy`.

Examples
========

The example below is excerpted from `example/plugins/c-api/client_context_dump/client_context_dump.cc` in the Traffic 
Server source distribution. It demonstrates how to use :func:`TSSslClientContextsNamesGet` and 
:func:`TSSslClientContextFindByName` to retreive all contextxs.

.. literalinclude:: ../../../../example/plugins/c-api/client_context_dump/client_context_dump.cc
  :language: c
  :lines: 137-145
