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

TSmalloc
********

Traffic Server memory allocation API.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void * TSmalloc(size_t size)
.. function:: void * TSrealloc(void * ptr , size_t size)
.. function:: char * TSstrdup(const char * str)
.. function:: char * TSstrndup(const char * str, size_t size)
.. function:: size_t TSstrlcpy(char * dst , const char * src , size_t size)
.. function:: size_t TSstrlcat(char * dst , const char * src , size_t size)
.. function:: void TSfree(void * ptr)

Description
===========

Traffic Server provides a number of routines for allocating and freeing
memory. These routines correspond to similar routines in the C library.
For example, :func:`TSrealloc` behaves like the C library routine :code:`realloc`.
There are two reasons to use the routines provided by Traffic Server. The
first is portability. The Traffic Server API routines behave the same on
all of Traffic Servers supported platforms. For example, :code:`realloc` does
not accept an argument of ``NULL`` on some platforms. The second reason is
that the Traffic Server routines actually track the memory allocations by
file and line number. This tracking is very efficient, is always turned
on, and is useful for tracking down memory leaks.

:func:`TSmalloc` returns a pointer to size bytes of memory allocated from the
heap. Traffic Server uses :func:`TSmalloc` internally for memory allocations.
Always use :func:`TSfree` to release memory allocated by :func:`TSmalloc`; do not use
:code:`free`.

:func:`TSstrdup` returns a pointer to a new string that is a duplicate of the
string pointed to by str. The memory for the new string is allocated using
:func:`TSmalloc` and should be freed by a call to :func:`TSfree`.
:func:`TSstrndup` returns a pointer to a new string that is a duplicate of the
string pointed to by :arg:`str` but is at most :arg:`size` bytes long. The new
string will be NUL-terminated. This API is very useful for transforming non
NUL-terminated string values returned by APIs such as
:func:`TSMimeHdrFieldValueStringGet` into NUL-terminated string values. The
memory for the new string is allocated using :func:`TSmalloc` and should be
freed by a call to :func:`TSfree`.

:func:`TSstrlcpy` copies up to :arg:`size` - 1 characters from the NUL-terminated
string :arg:`src` to :arg:`dst`, NUL-terminating the result.

:func:`TSstrlcat` appends the NUL-terminated string :arg:`src` to the end of :arg:`dst`. It
will append at most :arg:`size` - :code:`strlen(dst)` - 1 bytes, NUL-terminating the
result.

:func:`TSfree` releases the memory allocated by :func:`TSmalloc` or :func:`TSrealloc`. If
ptr is ``NULL``, :func:`TSfree` does no operation.

See also
========

:manpage:`TSAPI(3ts)`
