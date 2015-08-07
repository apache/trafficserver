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

.. default-domain:: c

==================
TSIOBufferReader
==================

Traffic Server IO buffer reader API.

Synopsis
========
`#include <ts/ts.h>`

.. function:: TSIOBufferReader TSIOBufferReaderAlloc(TSIOBuffer bufp)
.. function:: TSIOBufferReader TSIOBufferReaderClone(TSIOBufferReader readerp);
.. function:: void TSIOBufferReaderFree(TSIOBufferReader readerp);
.. function:: void TSIOBufferReaderConsume(TSIOBufferReader readerp, int64_t nbytes);
.. function:: TSIOBufferBlock TSIOBufferReaderStart(TSIOBufferReader readerp);
.. function:: int64_t TSIOBufferReaderAvail(TSIOBufferReader readerp);
.. function:: int64_t TSIOBufferReaderRead(TSIOBufferReader reader, const void * buf, int64_t length)
.. function:: bool TSIOBufferReaderIterate(TSIOBufferReader reader, TSIOBufferBlockFunc* func, void* context)

.. :type:: TSIOBufferBlockFunc

   ``bool (*TSIOBufferBlockFunc)(void const* data, size_t nbytes, void* context)``

   :arg:`data` is the data in the :type:`TSIOBufferBlock` of size :arg:`nbytes`. :arg:`context` is
   opaque data provided to the API call along with this function and passed on to the function. This
   function should return ``true`` to continue iterationa and ``false`` to terminate iteration.

Description
===========

The :type:`TSIOBufferReader` is an read accessor for :type:`TSIOBuffer`. It represents a location in
the contents of the buffer. A buffer can have multiple readers and each reader consumes data in the
buffer independently. Data which for which there are no readers is discarded from the buffer. This
has two very important consequences --

* Data for which there are no readers and no writer will be discarded. In effect this means without
   any readers only the current write buffer block will be maintained, older buffer blocks will
   disappear.
*  Conversely keeping a reader around unused will pin the buffer data in memory. This can be useful or harmful.

A buffer has a fixed amount of possible readers (currently 5) which is determined at compile
time. Reader allocation is fast and cheap until this maxium is reached at which point it fails.

:func:`TSIOBufferReaderAlloc` allocates a reader for the IO buffer :arg:`bufp`. This should only be
      called on a newly allocated buffer. If not the location of the reader in the buffer will be
      indeterminate. Use :func:`TSIOBufferReaderClone` to get another reader if the buffer is
      already in use.

:func:`TSIOBufferReaderClone` allocates a reader and sets its position in the buffer to be the same as :arg:`reader`.

:func:`TSIOBufferReaderFree` de-allocates the reader. Any data referenced only by this reader is
      discared from the buffer.

:func:`TSIOBufferReaderConsume` advances the position :arg:`reader` in its IO buffer by the
      the smaller of :arg:`bytes` and the maximum available in its IO buffer.

:func:`TSIOBufferReaderStart` returns the IO buffer block containing the position of
:arg:`reader`. Note the position of :arg:`reader` is in the block but not necessarily the first byte
of the block.

:func:`TSIOBufferReaderAvail` returns the number of bytes in the buffer which :arg:`reader` could
      consume. That is the number of bytes in the IO buffer past the current position of
      :arg:`reader`.

:func:`TSIOBufferReaderRead` reads data from :arg:`reader`. This first copies data from the IO
      buffer for :arg:`reader` to the target buffer :arg:`bufp`, starting at :arg:`reader`s
      position, and then advances (as with :func:`TSIOBufferReaderConsume`) :arg:`reader`s
      position past the copied data. The amount of data read in this fashion is the smaller of the
      amount of data available in the IO buffer for :arg:`reader` and the size of the target buffer
      (:arg:`length`).

:func:`TSIOBufferReaderIterate` iterates over the blocks for :arg:`reader`. For each block
:arg:`func` is called with with the data for the block and :arg:`context`. The :arg:`context` is an
opaque type to this function and is passed unchanged to :arg:`func`. It is intended to be used as
context for :arg:`func`. If :arg:`func` returns ``false`` the iteration terminates. The return value
is the return value from the last call to :arg:`func`.

.. note:: Destroying a :type:`TSIOBuffer` will de-allocate and destroy all readers for that buffer.



See also
========

:manpage:`TSIOBufferCreate(3ts)`
