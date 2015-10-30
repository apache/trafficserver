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

.. _developer-plugins-io-buffers:

IO Buffers
**********

The IO buffer data structure is the building block of the vconnection
abstraction. An **IO buffer** (``TSIOBuffer``) is composed of a list of
buffer blocks that point to buffer data. Both the buffer block
(``TSIOBufferBlock``) and buffer data (``TSIOBufferData``) data
structures are reference-counted, so they can reside in multiple buffers
at the same time. This makes it extremely efficient to copy data from
one IO buffer to another via ``TSIOBufferCopy``, since Traffic Server
must only copy pointers and adjust reference counts appropriately (and
doesn't actually copy any data).

The IO buffer abstraction provides for a single writer and multiple
readers. In order for the readers to have no knowledge of each other,
they manipulate IO buffers through the ``TSIOBufferReader`` data
structure. Since only a single writer is allowed, there is no
corresponding ``TSIOBufferWriter`` data structure. The writer simply
modifies the IO buffer directly. To see an example that illustrates how
to use IOBuffers, refer to the sample code in the description of
:c:func:`TSIOBufferBlockReadStart`.

Additional information about IO buffer functions:

-  The ``TSIOBufferReader`` data structure tracks how much data in
   ``TSIOBuffer`` has been read. It has an offset number of bytes that
   is the current start point of a particular buffer reader (for every
   read operation on an ``TSIOBuffer``, you must allocate an
   ``TSIOBufferReader``).

-  Bytes that have already been read may not necessarily be freed within
   the ``TSIOBuffer``. To consume bytes that have been read, you must
   call ``TSIOBufferConsume``.


