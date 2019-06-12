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

TSIOBufferCreate
****************

Traffic Server IO buffer API.

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSIOBuffer TSIOBufferCreate(void)
.. function:: TSIOBuffer TSIOBufferSizedCreate(TSIOBufferSizeIndex index)
.. function:: void TSIOBufferDestroy(TSIOBuffer bufp)
.. function:: int64_t TSIOBufferWrite(TSIOBuffer bufp, const void * buf, int64_t length)
.. function:: void TSIOBufferProduce(TSIOBuffer bufp, int64_t nbytes)
.. function:: int64_t TSIOBufferWaterMarkGet(TSIOBuffer bufp)
.. function:: void TSIOBufferWaterMarkSet(TSIOBuffer bufp, int64_t water_mark)

Description
===========

The :type:`TSIOBuffer` data structure is the building block of the
:type:`TSVConn`
abstraction. An IO buffer is composed of a list of buffer blocks which
are reference counted so that they can reside in multiple buffers at the
same time. This makes it extremely efficient to copy data from one IO
buffer to another using :func:`TSIOBufferCopy` since Traffic Server only needs to
copy pointers and adjust reference counts appropriately and not actually
copy any data. However, applications should still strive to ensure data
blocks are a reasonable size.

The IO buffer abstraction provides for a single writer and multiple
readers. In order for the readers to have no knowledge of each
other, they manipulate IO buffers through the :type:`TSIOBufferReader`
data structure. Since only a single writer is allowed, there is no
corresponding `TSIOBufferWriter` data structure. The writer
simply modifies the IO buffer directly.

:func:`TSIOBufferCreate` creates an empty :type:`TSIOBuffer`.

:func:`TSIOBufferSizedCreate` creates an empty :type:`TSIOBuffer`
with an initial capacity of :arg:`index` bytes.

:func:`TSIOBufferDestroy` destroys the IO buffer :arg:`bufp`. Since multiple IO
buffers can share data, this does not necessarily free all of the data
associated with the IO buffer but simply decrements the appropriate reference counts.

:func:`TSIOBufferWrite` appends :arg:`length` bytes from the buffer :arg:`buf` to the IO
buffer :arg:`bufp` and returns the number of bytes successfully written into the
IO buffer.

:func:`TSIOBufferProduce` makes :arg:`nbytes` of data available for reading in the IO
buffer :arg:`bufp`. A common pattern for writing to an IO buffer is to copy
data into a buffer block and then call INKIOBufferProduce to make the new
data visible to any readers.

.. note::

   The above references an old API function: INKIOBufferProduce and needs to
   be fixed. I don't see a TSIOBufferProduce function that would be its
   obvious replacement from the Ink->TS rename.

The watermark of an :type:`TSIOBuffer` is the minimum number of bytes of data
that have to be in the buffer before calling back any continuation that
has initiated a read operation on this buffer. As a writer feeds data
into the :type:`TSIOBuffer`, no readers are called back until the amount of data
reaches the watermark. Setting a watermark can improve performance
because it avoids frequent callbacks to read small amounts of data.
:func:`TSIOBufferWaterMarkGet` gets the current watermark for the IO buffer
:arg:`bufp`.

:func:`TSIOBufferWaterMarkSet` gets the current watermark for the IO buffer
:arg:`bufp` to :arg:`water_mark` bytes.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSIOBufferReaderAlloc(3ts)`
