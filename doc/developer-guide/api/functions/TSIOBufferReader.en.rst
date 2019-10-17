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
.. function:: TSIOBufferReader TSIOBufferReaderClone(TSIOBufferReader readerp)
.. function:: void TSIOBufferReaderFree(TSIOBufferReader readerp)
.. function:: void TSIOBufferReaderConsume(TSIOBufferReader readerp, int64_t nbytes)
.. function:: TSIOBufferBlock TSIOBufferReaderStart(TSIOBufferReader readerp)
.. function:: int64_t TSIOBufferReaderAvail(TSIOBufferReader readerp)
.. function:: bool TSIOBufferReaderIsAvailAtLeast(TSIOBufferReader, int64_t nbytes)
.. function:: int64_t TSIOBufferReaderCopy(TSIOBufferReader reader, void * buf, int64_t length)
.. function:: bool TSIOBufferReaderIterate(TSIOBufferReader reader, TSIOBufferBlockFunc* func, void* context)

.. type:: TSIOBufferBlockFunc

   ``bool (*TSIOBufferBlockFunc)(void const* data, int64_t nbytes, void* context)``

   :arg:`data` is the data in the :type:`TSIOBufferBlock` and is :arg:`nbytes` long. :arg:`context` is
   opaque data provided to the API call along with this function and passed on to the function. This
   function should return ``true`` to continue iteration or ``false`` to terminate iteration.

Description
===========

:type:`TSIOBufferReader` is an read accessor for :type:`TSIOBuffer`. It represents a location in
the contents of the buffer. A buffer can have multiple readers and each reader consumes data in the
buffer independently. Data which for which there are no readers is discarded from the buffer. This
has two very important consequences --

*  Data for which there are no readers and no writer will be discarded. In effect this means without
   any readers only the current write buffer block will be maintained, older buffer blocks will
   disappear.

*  Conversely keeping a reader around unused will pin the buffer data in memory. This can be useful
   or harmful.

A buffer has a fixed amount of possible readers (currently 5) which is determined at compile
time. Reader allocation is fast and cheap until this maximum is reached at which point it fails.

:func:`TSIOBufferReaderAlloc` allocate a reader.
   A reader for the IO buffer :arg:`bufp` is created and returned. This should only be called on a
   newly allocated buffer. If not the location of the reader in the buffer will be indeterminate.
   Use :func:`TSIOBufferReaderClone` to get another reader if the buffer is already in use.

:func:`TSIOBufferReaderClone` duplicate a reader.
   A reader for :arg:`bufp` is allocated and the initial reader position is set to be the same as
   :arg:`reader`.

:func:`TSIOBufferReaderFree` de-allocate :arg:`reader`.
   This also effectively consumes (see :func:`TSIOBufferReaderConsume`) all data for :arg:`reader`.

:func:`TSIOBufferReaderConsume` consume data from :arg:`reader`.
   This advances the position of :arg:`reader` in its IO buffer by the the smaller of :arg:`nbytes`
   and the maximum available in the buffer. This is required to release the buffer memory - when
   data has been consumed by all readers, it is discarded.

:func:`TSIOBufferReaderStart` Get the first buffer block.
   This returns the :type:`TSIOBufferBlock` which contains the first byte available to :arg:`reader`.

   .. note:: The byte at the position of :arg:`reader` is in the block but is not necessarily the first byte of the block.

:func:`TSIOBufferReaderAvail` returns the number of bytes available.
   The bytes available is the amount of data that could be read from :arg:`reader`.

:func:`TSIOBufferReaderIsAvailAtLeast` - check amount of data available.
   This function returns :code:`true` if the available number of bytes for :arg:`reader` is at least
   :arg:`nbytes`, :code:`false` if not. This can be much more efficient than
   :func:`TSIOBufferReaderAvail` because the latter must walk all the IO buffer blocks in the IO
   buffer. This function returns as soon as the return value can be determined. In particular a
   value of ``1`` for :arg:`nbytes` means only the first buffer block will be checked making the
   call very fast.

:func:`TSIOBufferReaderCopy` copies data from :arg:`reader` into :arg:`buff`.
   This copies data from the IO buffer for :arg:`reader` to the target buffer :arg:`bufp`. The
   amount of data read in this fashion is the smaller of the amount of data available in the IO
   buffer for :arg:`reader` and the size of the target buffer (:arg:`length`). The number of bytes
   copied is returned.

:func:`TSIOBufferReaderIterate` iterate over the blocks for :arg:`reader`.
  For each block :arg:`func` is called with with the data for the block and :arg:`context`. The
  :arg:`context` is an opaque type to this function and is passed unchanged to :arg:`func`. It is
  intended to be used as context for :arg:`func`. If :arg:`func` returns ``false`` the iteration
  terminates. If :arg:`func` returns true the block is consumed. The return value for
  :func:`TSIOBufferReaderIterate` is the return value from the last call to :arg:`func`.

.. note::

  If it would be a problem for the iteration to consume the data (especially in cases where
  :code:`false` might be returned) the reader can be cloned via :func:`TSIOBufferReaderClone` to
  keep the data in the IO buffer and available. If not needed the reader can be destroyed or
  if needed the original reader can be destroyed and replaced by the clone.

.. note:: Destroying a :type:`TSIOBuffer` will de-allocate and destroy all readers for that buffer.



See also
========

:manpage:`TSIOBufferCreate(3ts)`
