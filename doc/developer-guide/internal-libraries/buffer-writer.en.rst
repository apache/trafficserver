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
.. highlight:: cpp
.. default-domain:: cpp

.. _BufferWriter:

BufferWriter
*************

Synopsis
++++++++

.. code-block:: cpp

   #include <api/ts_bw_format.h> // Above plus Formatting support.

Description
+++++++++++

:code:`BufferWriter` is intended to increase code reliability and reduce complexity in the common
circumstance of generating formatted output strings in fixed buffers. Current usage is a mixture of
:code:`snprintf` and :code:`memcpy` which provides a large scope for errors and verbose code to
check for buffer overruns. The goal is to provide a wrapper over buffer size tracking to make such
code simpler and less vulnerable to implementation error.

:code:`BufferWriter` itself is an abstract class to describe the base interface to wrappers for
various types of output buffers. As a common example, :code:`FixedBufferWriter` is a subclass
designed to wrap a fixed size buffer. :code:`FixedBufferWriter` is constructed by passing it a
buffer and a size, which it then tracks as data is written. Writing past the end of the buffer is
clipped to prevent overruns.

Consider current code that looks like this.

.. code-block:: cpp

   char buff[1024];
   char * ptr = buff;
   size_t len = sizeof(buff);
   //...
   if (len > 0) {
     auto n = std::min(len, thing1_len);
     memcpy(ptr, thing1, n);
     len -= n;
   }
   if (len > 0) {
     auto n = std::min(len, thing2_len);
     memcpy(ptr, thing2, n);
     len -= n;
   }
   if (len > 0) {
     auto n = std::min(len, thing3_len);
     memcpy(ptr, thing3, n);
     len -= n;
   }

This is changed to

.. code-block:: cpp

   char buff[1024];
   swoc::FixedBufferWriter bw(buff, sizeof(buff));
   //...
   bw.write(thing1, thing1_len);
   bw.write(thing2, thing2_len);
   bw.write(thing3, thing3_len);

The remaining length is updated every time and checked every time. A series of checks, calls to
:code:`memcpy`, and size updates become a simple series of calls to :code:`BufferWriter::write`.

More in depth documentation is in the libswoc documentation.

Which header to include depends on usage.

"ts_bw.h"
   Contains on the basic buffer manipulation.
"ts_bw_format.h"
   Basic buffer manipulation and formatting. This does not include IP address support, which is pulled in
   with the "ts_ip.h" header.

Usage
+++++

:code:`BufferWriter` is an abstract base class, in the style of :code:`std::ostream`. There are
several subclasses for various use cases. When passing around this is the common type.

:code:`FixedBufferWriter` writes to an externally provided buffer of a fixed length. The buffer must
be provided to the constructor. This will generally be used in a function where the target buffer is
external to the function or already exists.

:code:`LocalBufferWriter` is a templated class whose template argument is the size of an internal
buffer. This is useful when the buffer is local to a function and the results will be transferred
from the buffer to other storage after the output is assembled. Rather than having code like::

   char buff[1024];
   swoc::FixedBufferWriter bw(buff, sizeof(buff));

it can be written more compactly as::

   swoc::LocalBufferWriter<1024> bw;

In many cases, when using :code:`LocalBufferWriter` this is the only place the size of the buffer
needs to be specified and therefore can simply be a constant without the overhead of defining a size
to maintain consistency. The choice between :code:`LocalBufferWriter` and :code:`FixedBufferWriter`
comes down to the owner of the buffer - the former has its own buffer while the latter operates on
a buffer owned by some other object. Therefore if the buffer is declared locally, use
:code:`LocalBufferWriter` and if the buffer is received from an external source (such as via a
function parameter) use :code:`FixedBufferWriter`.

For convenience and performance there is a thread local string, :code:`ts::bw_dbg` which can be used as
an expanding buffer for ``BufferWriter``. If the output is too large for the string storage, that storage
is increased to be sufficient to hold the output which is generated again. Because the storage is never
decreased over time the string becomes large enough that no further allocation is needed.
