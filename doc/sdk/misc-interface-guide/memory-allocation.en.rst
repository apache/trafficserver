Memory Allocation
*****************

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

Traffic Server provides five routines for allocating and freeing memory.
These routines correspond to similar routines in the C library. For
example, ``TSrealloc`` behaves like the C library routine ``realloc``.

There are two main reasons for using the routines provided by Traffic
Server. The first is portability: the Traffic Server API routines behave
the same on all of Traffic Server's supported platforms. For example,
``realloc`` does not accept an argument of ``NULL`` on some platforms.
The second reason is that the Traffic Server routines actually track the
memory allocations by file and line number. This tracking is very
efficient, always turned on, and quite useful when tracking down memory
leaks.

The memory allocation functions are:

-  :c:func:`TSfree`

-  :c:func:`TSmalloc`

-  :c:func:`TSrealloc`

-  :c:func:`TSstrdup`

-  :c:func:`TSstrndup`


