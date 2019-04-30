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

.. _developer-plugins-http-transformations:

HTTP Transformations
********************

Transform plugins examine or transform HTTP message body content. For
example, transform plugins can:

-  Append text to HTML documents

-  Compress images

-  Do virus checking (on client ``POST`` data or server response data)

-  Do content-based filtering (filter out HTML documents that contain
   certain terms or expressions)

This chapter explains how to write transform plugins. The following
examples are discussed in detail:

.. toctree::
   :maxdepth: 2

   sample-null-transformation-plugin.en
   append-transform-plugin.en
   sample-buffered-null-transformation-plugin.en

.. _WritingContentTransformPlugin:

Writing Content Transform Plugins
=================================

Content transformation plugins transform HTTP response content (such as
images or HTML documents) and HTTP request content (such as client
``POST`` data). Because the data stream to be transformed is of variable
length, these plugins must use a mechanism that passes data from buffer
to buffer *and* checks to see if the end of the data stream is reached.
This mechanism is provided by virtual connections (``VConnection``\ s)
and virtual IO descriptors (``VIO``\ s).

A ``VConnection`` is an abstraction for a data pipe that allows its
users to perform asynchronous reads and writes without knowing the
underlying implementation. A transformation is a specific type of
``VConnection``. A **transformation** connects an input data source and
an output data sink; this feature enables it to view and modify all the
data passing through it.

Transformations can be chained together, one after the other, so that
multiple transformations can be performed on the same content. The
``VConnection`` type, ``TSVConn``, is actually a subclass of ``TSCont``,
which means that ``VConnection``\ s (and transformations) are
continuations. ``VConnection``\ s and transformations can thus exchange
events, informing one another that data is available for reading or
writing, or that the end of a data stream is reached.

A ``VIO`` is a description of an IO operation that is in progress.
Every ``VConnection`` has an associated *input VIO* and an associated
*output VIO*. When ``VConnection``\ s are transferring data to one
another, one ``VConnection``'s input ``VIO`` is another
``VConnection``'s output ``VIO``. A ``VConnection``'s input ``VIO`` is
also called its **write ``VIO``** because the input ``VIO`` refers to a
write operation performed on the ``VConnection`` itself. Similarly, the
output ``VIO`` is also called the **read ``VIO``**. For transformations,
which are designed to pass data in one direction, you can picture the
relationship between the transformation ``VConnection`` and its
``VIO``\ s as follows:

.. _transformationAndItsVIOs:

.. figure:: /static/images/sdk/vconnection.jpg
   :alt: A Transformation and its VIOs
   :align: center

   **A Transformation and its VIOs**

Because the Traffic Server API places transformations directly in the
response or request data stream, the transformation ``VConnection`` is
responsible only for reading the data from the input buffer,
transforming it, and then writing it to the output buffer. The upstream
``VConnection`` writes the incoming data to the transformation's input
buffer. In the figure above, :ref:`TransformationAndItsVIOs`, the input ``VIO`` describes the
progress of the upstream ``VConnection``'s write operation on the
transformation, while the output ``VIO`` describes the progress of the
transformation's write operation on the output (downstream)
``VConnection``. The **nbytes** value in the ``VIO`` is the total number
of bytes to be written. The **ndone** value is the current progress, or
the number of bytes that have been written at a specific point in time.

When writing a transformation plugin, you must understand implementation
as well as the use of ``VConnection``\ s. The *implementer's side*
refers to how to implement a ``VConnection`` that others can use. At
minimum, a transform plugin creates a transformation that sits in the
data stream and must be able to handle the events that the upstream and
downstream ``VConnection``\ s send to it. The *user's side* refers to
how to use a ``VConnection`` to read or write data. At the very least,
transformations output (write) data.

.. _transformations:

Transformations
---------------

VIOs
----

A ``VIO`` or virtual IO is a description of an in progress IO
operation. The ``VIO`` data structure is used by ``VConnection`` users
to determine how much progress has been made on a particular IO
operation, and to reenable an IO operation when it stalls due to buffer
space. ``VConnection`` implementers use ``VIO``\ s to determine the
buffer for an IO operation, how much work to do on the IO operation, and
which continuation to call back when progress on the IO operation is
made.

The ``TSVIO`` data structure itself is opaque, but it might have been
defined as follows:

.. code-block:: c

   typedef struct {
      TSCont continuation;
      TSVConn vconnection;
      TSIOBufferReader reader;
      TSMutex mutex;
      int nbytes;
      int ndone;
   } *TSVIO;

IO Buffers
----------

The **IO buffer** data structure is the building block of the
``VConnection`` abstraction. An IO buffer is composed of a list of
buffer blocks which, in turn, point to buffer data. Both the *buffer
block* (``TSIOBufferBlock``) and *buffer data* (``TSIOBufferData``) data
structures are reference counted so they can reside in multiple buffers
at the same time. This makes it extremely efficient to copy data from
one IO buffer to another using ``TSIOBufferCopy``, since Traffic Server
only needs to copy pointers and adjust reference counts appropriately
(instead of actually copying any data).

The IO buffer abstraction provides for a single writer and multiple
readers. In order for the readers to have no knowledge of each other,
they manipulate IO buffers through the\ ``TSIOBufferReader`` data
structure. Since only a single writer is allowed, there is no
corresponding ``TSIOBufferWriter`` data structure. The writer simply
modifies the IO buffer directly.

Transaction Data Sink
~~~~~~~~~~~~~~~~~~~~~

The hook `TS_HTTP_RESPONSE_CLIENT_HOOK` is a hook that supports a special type of transformation, one with only input and no output.
Although the transformation doesn't provide data back to Traffic Server it can do anything else with the data, such as writing it
to another output device or process. It must, however, consume all the data for the transaction. There are two primary use cases.

#. Tap in to the transaction to provide the data for external processing.
#. Maintain the transaction.

For the latter it is important to note that if all consumers of a transaction (primarily the user agent) shut down the transaction is also
terminated, including the connection to the origin server. A data sink transform, unlike a standard transform, is considered to be a consumer
and will keep the transaction and the origin server connection up. This is useful when the transaction is in some way expensive and should
run to completion even if the user agent disconnects. Examples would be a standard transform that is expensive to initiate, or expensive
origin server connections that should be :ts:cv:`shared <proxy.config.http.server_session_sharing.match>`.

There is an `example plugin <https://github.com/apache/trafficserver/blob/master/example/txn_data_sink/txn_data_sink.c>`_ that demonstrates
this used as a pure data sink to keep the transaction up regardless of whether the user agent disconnects.
