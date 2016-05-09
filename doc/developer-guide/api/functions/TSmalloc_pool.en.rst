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

.. default-domain:: c

TSmalloc_pool
********

Traffic Server memory allocation API using underlying IOBuf memory pools.

Synopsis
========

`#include <ts/ts.h>`

.. function:: void * TSmalloc_pool(size_t size)
.. function:: void * TSrealloc_pool(void * ptr , size_t size)
.. function:: void TSfree_pool(void * ptr)

Description
===========

:func:`TSmalloc_pool` returns a pointer to size bytes of memory allocated from the
IOBuf memory pools. You must always use :func:`TSfree_pool` to free memory allocated
via :func:`TSmalloc_pool` or :func:`TSrealloc_pool`.

See also
========

:manpage:`TSAPI(3ts)`
