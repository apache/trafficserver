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

.. _developer-plugins-io-transformations:

Transformations
***************

Vconnection Implementor's View
==============================

A VConnection implementor writes only transformations. All other
VConnections (net VConnections and cache VConnections) are implemented
in iocore. As mentioned earlier, a given vconnection can have a maximum
of one read operation and one write operation being performed on it. The
vconnection user gets information about the operation being performed by
examining the VIO returned by a call to :c:func:`TSVConnRead` or
:c:func:`TSVConnWrite`. The implementor, in turn, gets a handle on the VIO
operation by examining the VIO returned by :c:func:`TSVConnReadVIOGet` or
:c:func:`TSVConnWriteVIOGet` (recall that every vconnection created through
the Traffic Server API has an associated read VIO and write VIO, even if
it only supports reading or writing).

For example, the null transform plugin's transformation examines the
input VIO by calling:

.. code-block:: c

     input_vio = TSVConnWriteVIOGet (contp);

where ``contp`` is the transformation.

A vconnection is a continuation. This means it has a handler function
that is run when an event is sent to it, or more accurately, when an
event that was sent to it is received. It is the handler function's job
to examine the event, the current state of its read VIO and write VIO,
and any other internal state the vconnection might have and potentially
make some progress on the IO operations.

It is common for the handler function for all vconnections to look
similar. Their basic form looks something like the code fragment below:

.. code-block:: c

    int
    vconnection_handler (TSCont contp, TSEvent event, void *edata)
    {
    if (TSVConnClosedGet (contp)) {
            /* Destroy any vconnection specific data here. */
            TSContDestroy (contp);
            return 0;
       } else {
            /* Handle the incoming event */
       }
    }

This code fragment basically shows that many vconnections simply want to
destroy themselves when they are closed. However, the situation might
also require the vconnection to do some cleanup processing - which is
why :c:func:`TSVConnClose` does not simply just destroy the vconnection.

Vconnections are state machines that are animated by the events they
receive. An event is sent to the vconnection whenever an
:c:func:`TSVConnRead`, :c:func:`TSVConnWrite`, :c:func:`TSVConnClose`,
:c:func:`TSVConnShutdown`, or :c:func:`TSVIOReenable` call is performed.
:c:func:`TSVIOReenable` indirectly references the vconnection through a
back-pointer in the VIO structure to the vconnection. The vconnection
itself only knows which call was performed by examining its state and
the state of its VIOs. For example, when :c:func:`TSVConnClose` is called, the
vconnection is sent an immediate event (``TS_EVENT_IMMEDIATE``). For
every event the vconnection receives, it needs to check its closed flag
to see if it has been closed. Similarly, when :c:func:`TSVIOReenable` is
called, the vconnection is sent an immediate event. For every event the
vconnection receives, it must check its VIOs to see if the buffers have
been modified to a state in which it can continue processing one of its
operations.

Finally, a vconnection is likely the user of other vconnections. It also
receives events as the user of these other vconnections. When it
receives such an event, like ``TS_EVENT_VCONN_WRITE_READY``, it might
just enable another vconnection that's writing into the buffer used by
the vconnection reading from it. The above description is merely
intended to give the overall idea for what a vconnection needs to do.

Transformation VConnection
--------------------------

A :ref:`transformation <transformations>` is
a specific type of vconnection. It supports a subset of the vconnection
functionality that enables one or more transformations to be chained
together. A transformation sits as a bottleneck between an input data
source and an output data sink, which enables it to view and modify all
the data passing through it. Alternatively, some transformations simply
scan the data and pass it on. A common transformation is one that
compresses data in some manner.

A transformation can modify either the data stream being sent *to* an
HTTP client (e.g. the document) or the data stream being sent *from* an
HTTP client (e.g. post data). To do this, the transformation should hook
on to one of the following hooks:

-  ``TS_HTTP_REQUEST_TRANSFORM_HOOK``

-  ``TS_HTTP_RESPONSE_TRANSFORM_HOOK``

Note that because a transformation is intimately associated with a given
transaction, it is only possible to add the hook to the transaction
hooks - not to the global or session hooks. Transformations reside in a
chain, so their ordering is quite easily determined: transformations
that add themselves to the chain are simply appended to it.

Data is passed in to the transformation by initiating a vconnection
write operation on the transformation. As a consequence of this design,
a transformation must support the vconnection write operation. In other
words, your transformation must expect an upstream vconnection to write
data to it. The transformation has to read the data, consume it, and
tell the upstream vconnection it is finished by sending it an
``TS_EVENT_WRITE_COMPLETE`` event. Transformations cannot send the
``TS_EVENT_VCONN_WRITE_COMPLETE`` event to the upstream vconnection
unless they are finished consuming all incoming data. If
``TS_EVENT_VCONN_WRITE_COMPLETE`` is sent prematurely, then certain
internal Traffic Server data structures will not be deallocated, thereby
causing a memory leak.

Here's how to make sure that all incoming data is consumed:

-  After reading or copying data, make sure that you consume the data
   and increase the value of ndone for the input VIO, as in the
   following example taken from ``null_transform.c``:

   .. code-block:: c

       TSIOBufferCopy (TSVIOBufferGet (data->output_vio),
       TSVIOReaderGet (input_vio), towrite, 0);
       /* Tell the read buffer that we have read the data and are no longer interested in it. */
       TSIOBufferReaderConsume (TSVIOReaderGet (input_vio), towrite);
       /* Modify the input VIO to reflect how much has been read.*/
       TSVIONDoneSet (input_vio, TSVIONDoneGet (input_vio) + towrite);

-  Before sending ``TS_EVENT_VCONN_WRITE_COMPLETE``, your transformation
   should check the number of bytes remaining in the upstream
   vconnection's write VIO (input VIO) using the function
   ``TSVIONTodoGet`` (``input_vio``). This value should go to zero when
   all of the upstream data is consumed
   (``TSVIONTodoGet = nbytes - ndone``). Do not send
   ``TS_EVENT_VCONN_WRITE_COMPLETE`` events if :c:func:`TSVIONTodoGet` is
   greater than zero.
-  The transformation passes data out of itself by using the output
   vconnection retrieved by :c:func:`TSTransformOutputVConnGet`. Immediately
   before Traffic Server initiates the write operation (which inputs
   data into the transformation), it sets the output vconnection either
   to the next transformation in the chain of transformations or to a
   special terminating transformation (if it's the last transformation
   in the chain). Since the transformation is handed ownership of the
   output vconnection, it must close it at some point in order for it to
   be deallocated.
-  All of the transformations in a transformation chain share the
   transaction's mutex. This small restriction (enforced by
   :c:func:`TSTransformCreate`) removes many of the locking complications of
   implementing general vconnections. For example, a transformation does
   not have to grab its write VIO mutex before accessing its write VIO
   because it knows it already holds the mutex.

The transformation functions are:

  - :c:func:`TSTransformCreate`
  - :c:func:`TSTransformOutputVConnGet`
