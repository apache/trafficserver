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

.. include:: ../../common.defs

.. _developer-plugins-interfaces:

Plugin Interfaces
*****************

Most of the functions in the Traffic Server API provide an interface to
specific code modules within Traffic Server. The miscellaneous functions
described in this chapter provide some useful general capabilities. They
are categorized as follows:

.. toctree::
   :maxdepth: 2

The C library already provides functions such as ``printf``, ``malloc``,
and ``fopen`` to perform these tasks. The Traffic Server API versions,
however, overcome various C library limitations (such as portability to
all Traffic Server-support platforms).

.. _developer-plugins-tsfopen-family:

TSfopen Family
==============

The ``fopen`` family of functions in C is normally used for reading
configuration files, since ``fgets`` is an easy way to parse files on a
line-by-line basis. The ``TSfopen`` family of functions aims at solving
the same problem of buffered IO and line at a time IO in a
platform-independent manner. The ``fopen`` family of C library functions
can only open a file if a file descriptor less than 256 is available.
Since Traffic Server often has more than 2000 file descriptors open at
once, however, the likelihood of an available file descriptor less than
256 very small. To solve this problem, the ``TSfopen`` family can open
files with descriptors greater than 256.

The ``TSfopen`` family of routines is not intended for high speed IO or
flexibility - they are blocking APIs (not asynchronous). For performance
reasons, you should not directly use these APIs on a Traffic Server
thread (when being called back on an HTTP hook); it is better to use a
separate thread for doing the blocking IO. The ``TSfopen`` family is
intended for reading and writing configuration information when
corresponding usage of the ``fopen`` family of functions is
inappropriate due to file descriptor and portability limitations. The
``TSfopen`` family of functions consists of the following:

-  :c:func:`TSfclose`

-  :c:func:`TSfflush`

-  :c:func:`TSfgets`

-  :c:func:`TSfopen`

-  :c:func:`TSfread`

-  :c:func:`TSfwrite`

Memory Allocation
=================


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

Thread Functions
================

The Traffic Server API thread functions enable you to create, destroy,
and identify threads within Traffic Server. Multithreading enables a
single program to have more than one stream of execution and to process
more than one transaction at a time. Threads serialize their access to
shared resources and data using the ``TSMutex`` type, as described in
:ref:`developer-plugins-mutexes`.

The thread functions are listed below:

-  :c:func:`TSThreadCreate`
-  :c:func:`TSThreadDestroy`
-  :c:func:`TSThreadInit`
-  :c:func:`TSThreadSelf`

Debugging Functions
===================

-  :c:func:`TSDebug`
   prints out a formatted statement if you are running Traffic Server in
   debug mode.

-  :c:func:`TSIsDebugTagSet`
   checks to see if a debug tag is set. If the debug tag is set, then
   Traffic Server prints out all debug statements associated with the
   tag.

-  :c:func:`TSError`
   prints error messages to Traffic Server's error log

-  :c:func:`TSAssert`
   enables the use of assertion in a plugin.

-  :c:func:`TSReleaseAssert`
   enables the use of assertion in a plugin.

