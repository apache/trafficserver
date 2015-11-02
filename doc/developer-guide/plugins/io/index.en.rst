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

.. _developer-plugins-io:

IO
**

This chapter contains the following sections:

.. toctree::
   :maxdepth: 2

   net-vconnections.en
   transformations.en
   vios.en
   io-buffers.en
   cache-api.en

.. _sdk-vconnections:

Vconnections
============

A User's Perspective
--------------------

To use a vconnection, a user must first get a handle to one. This is
usually accomplished by having it handed to the user; the user may also
simply issue a call that creates a vconnection (such as
``TSNetConnect)``. In the case of transform plugins, the plugin creates
a transformation vconnection viav ``TSTransformCreate`` and then
accesses the output vconnection using ``TSTransformOutputVConnGet``.

After getting a handle to a vconnection, the user can then issue a read
or write call. It's important to note that not all vconnections support
both reading and writing - as of yet, there has not been a need to query
a vconnection about whether it can perform a read or write operation.
That ability should be obvious from context.

To issue a read or write operation, a user calls ``TSVConnRead`` or
``TSVConnWrite``. These two operations both return ``VIO (TSVIO)``. The
VIO describes the operation being performed and how much progress has
been made. Transform plugins initiate output to the downstream
vconnection by calling ``TSVConnWrite``.

A vconnection read or write operation is different from a normal UNIX
``read(2)`` or ``write(2)`` operation. Specifically, the vconnection
operation can specify more data to be read or written than exists in the
buffer handed to the operation. For example, it's typical to issue a
read for ``INT64_MAX`` (9 quintillion) bytes from a network vconnection
in order to read all the data from the network connection until the end
of stream is reached. This contrasts with the usual UNIX fashion of
issuing repeated calls to ``read(2)`` until one of the calls finally
returns ``0`` to indicate the end of stream was reached (indeed, the
underlying implementation of vconnections on UNIX still does issue those
calls to ``read(2)``, but the interface does not expose that detail).

At most, a given vconnection can have one read operation and one write
operation being performed on it. This is restricted both by design and
common sense: if two write operations were performed on a single
vconnection, then the user would not be able to specify which should
occur first and the output would occur in an intermingled fashion. Note
that both a read operation and a write operation can happen on a single
vconnection at the same time; the restriction is for more than one
operation of the same type.

One obvious issue is that the buffer passed to ``TSVConnRead`` and
``TSVConnWrite`` won't be large enough - there is no reasonable way to
make a buffer that can hold ``INT64_MAX`` (9 quintillion) bytes! The
secret is that vconnections engage in a protocol whereby they signal to
the user (via the continuation passed to ``TSVConnRead`` and
``TSVConnWrite``) that they have emptied the buffers passed to them and
are ready for more data. When this occurs, it is up to the user to add
more data to the buffers (or wait for more data to be added) and then
wake up the vconnection by calling ``TSVIOReenable`` on the VIO
describing the operation. ``TSVIOReenable`` specifies that the buffer
for the operation has been modified and that the vconnection should
reexamine it to see if it can make further progress.

The null transform plugin provides an example of how this is done. Below
is a prototype for ``TSVConnWrite``:

.. code-block:: c

     TSVIO TSVConnWrite (TSVConn connp, TSCont contp, TSIOBufferReader readerp, int nbytes)

The ``connp`` is the vconnection the user is writing to and ``contp`` is
the "user" - i.e., the continuation that ``connp`` calls back when it
has emptied its buffer and is ready for more data.

The call made in the null transform plugin is:

.. code-block:: c

      TSVConnWrite (output_conn, contp, data->output_reader, TSVIONBytesGet (input_vio));

In the example above, ``contp`` is the transformation vconnection that
is writing to the output vconnection. The number of bytes to be written
is obtained from ``input_vio`` by ``TSVIONBytesGet``.

When a vconnection calls back its user to indicate that it wants more
data (or when some other condition has occurred), it issues a call to
``TSContCall``. It passes the ``TSVIO`` describing the operation as the
data parameter, and one of the values below as the event parameter.

``TS_EVENT_ERROR``
    Indicates an error has occurred on the vconnection. This will happen
    for network IO if the underlying ``read(2)`` or ``write(2)`` call
    returns an error.

``TS_EVENT_VCONN_READ_READY``
    The vconnection has placed data in the buffer passed to an
    ``TSVConnRead`` operation and it would like to do more IO, but the
    buffer is now full. When the user consumes the data from the buffer,
    this should re-enable the VIO so it indicates to the vconnection
    that the buffer has been modified.

``TS_EVENT_VCONN_WRITE_READY``
    The vconnection has removed data from the buffer passed to an
    ``TSVConnWrite`` operation and it would like to do more IO, but the
    buffer does not have enough data in it. When placing more data in
    the buffer, the user should re-enable the VIO so it indicates to the
    vconnection that the buffer has been modified.

``TS_EVENT_VCONN_READ_COMPLETE``
    The vconnection has read all the bytes specified by an
    ``TSVConnRead`` operation. The vconnection can now be used to
    initiate a new IO operation.

``TS_EVENT_VCONN_WRITE_COMPLETE``
    The vconnection has written all the bytes specified by an
    ``TSVConnWrite`` operation and can now be used to initiate a new IO
    operation.

``TS_EVENT_VCONN_EOS``
    An attempt was made to read past the end of the stream of bytes
    during the handling of an ``TSVConnRead`` operation. This event
    occurs when the number of bytes available for reading from a
    vconnection is less than the number of bytes the user specifies
    should be read from the vconnection in a call to ``TSVConnRead``. A
    common case where this occurs is when the user specifies that
    ``INT64_MAX`` bytes are to be read from a network connection.

For example: the null transform plugin's transformation receives
``TS_EVENT_VCONN_WRITE_READY`` and ``TS_EVENT_VCONN_WRITE_COMPLETE``
events from the downstream vconnection as a result of the call to
``TSVConnWrite``.

After using a vconnection, the user must call ``TSVConnClose`` or
``TSVConnAbort``. While both calls indicate that the vconnection can
destroy itself, ``TSVConnAbort`` should be used when the connection is
being closed abnormally. After a call to ``TSVConnClose`` or
``TSVConnAbort``, the user will not be called back by the vconnection
again.

Sometimes it's desirable to simply close down the write portion of a
connection while keeping the read portion open. This can be accomplished
via the ``TSVConnShutdown`` function, which shuts down either the read
or write portion of a vconnection. *Shutdown* means that the vconnection
will no longer call back the user with events for the portion of the
connection that was shut down. For example: if the user shuts down the
write portion of a connection, then the ``TS_EVENT_VCONN_WRITE_READY``
or ``TS_EVENT_VCONN_WRITE_COMPLETE`` events will not be produced. In the
null transform plugin, the write operation is shut down with a call to
``TSVConnShutdown``. To learn how vconnections are used in
transformation plugins, see :ref:`Writing Content Transform
Plugins <WritingContentTransformPlugin>`.

The vconnection functions are listed below:

-  :c:func:`TSVConnAbort`
-  :c:func:`TSVConnClose`
-  :c:func:`TSVConnClosedGet`
-  :c:func:`TSVConnRead`
-  :c:func:`TSVConnReadVIOGet`
-  :c:func:`TSVConnShutdown`
-  :c:func:`TSVConnWrite`
-  :c:func:`TSVConnWriteVIOGet`


