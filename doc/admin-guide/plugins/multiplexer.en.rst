.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements. See the NOTICE file distributed with
   this work for additional information regarding copyright ownership. The ASF
   licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
   WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
   License for the specific language governing permissions and limitations under
   the License.

.. include:: ../../common.defs

.. highlight:: cpp
.. default-domain:: cpp
.. |Name| replace:: Multiplexer

|Name|
**********

|Name| is a remap plug-in that allows requests to certain origins to be multiplexed (i.e.,
duplicated and dispatched in parallel) to one or more other hosts.  The headers are copied into the
new requests as well as POST bodies.  Optionally POST and PUT requests can be skipped via
``pparam=proxy.config.multiplexer.skip_post_put=1``.

Description
===========

|Name| does the following for requests it is configured to multiplex:

#. Add the ``X-Multiplexer: original`` header into the client's request.
#. For each host in ``pparam`` of the remap rule:

   #. Copies the resulting request (POST bodies are copied via the :ref:`HTTP Transform <developer-plugins-http-transformations>` mechanism).
   #. Changes the ``Host`` header of the copy according to ``pparam`` from the remap rule.
   #. Changes ``X-Multiplexer`` header value to ``copy`` instead of ``original``.
   #. Asynchronously sends the copied request with :c:func:`TSHttpConnect`.

|Name| dispatches the requests in the background without blocking the original request. Multiplexed
responses are drained and discarded. Note that these multiplexed requests are made using
:c:func:`TSHttpConnect` and are thus HTTP (not HTTPS) connections.

A default ``1`` second timeout is configured when communicating with each of the hosts receiving the
multiplexed requests. This timeout can be overwritten via the ``multiplexer__timeout`` environment
variable representing how many nanoseconds to wait.

The ``multiplexer`` debug tag can be used for debugging purposes (see
:ts:cv:`proxy.config.diags.debug.tags`). While debugging, multiplexed requests and responses are
printed into the logs.

|Name| produces the following statistics consumed with ``traffic_ctl``:

*  failures: number of failed multiplexed requests
*  hits: number of successful multiplexed requests
*  requests: total number of multiplexed requests
*  time(avg): average time taken between multiplexed requests and their responses
*  timeouts: number of multiplexed requests which timed-out
*  size(avg): average size of multiplexed responses

Here are some example :file:`remap.config` configuration lines::

    map http://www.example.com/a http://www.example.com/ @plugin=multiplexer.so @pparam=host1.example.com
    map http://www.example.com/b http://www.example.com/ @plugin=multiplexer.so @pparam=host2.example.com
    map http://www.example.com/c http://www.example.com/ @plugin=multiplexer.so @pparam=host1.example.com @pparam=host2.example.com
    map http://www.example.com/d http://www.example.com/ @plugin=multiplexer.so @pparam=host1.example.com @pparam=host2.example.com @pparam=proxy.config.multiplexer.skip_post_put=1

#. The first entry will multiplex requests sent to ``http://www.example.com`` with a path of ``/a``
   to ``host1.example.com``.
#. The second entry will multiplex requests sent to ``http://www.example.com`` with a path of ``/b``
   to ``host2.example.com``.
#. The third entry will multiplex requests sent to ``http://www.example.com`` with a path of ``/c``
   to both ``host1.example.com`` and ``host2.example.com``.
#. The fourth entry will multiplex requests sent to ``http://www.example.com`` with a path of ``/d``
   to both ``host1.example.com`` and ``host2.example.com``, but POST and PUT requests will
   not be multiplexed.

Implementation
==============

Parsing Chunk Encoded Data
--------------------------

|Name| parses chunked data with its own home brew parser. In the parser :code:`size_` is the size of
a chunk to be consumed. The local variable / parameter :code:`size` is raw input size as read from an
:code:`TSIOBufferBlock`. The "size states" are marked blue.

.. uml::
   :align: center

   @startuml

   skinparam state {
      backgroundColor<<SIZE_STATE>> SkyBlue
   }

   state kSize <<SIZE_STATE>>
   kSize : Accumulate size_\nfrom hex input.
   [*] --> kSize
   kSize --> kSize : hex digit
   kSize --> kDataN : CR,size_ > 0
   kSize --> kEndN : CR, size_ == 0
   kSize --> kInvalid : *

   state kDataN <<SIZE_STATE>>
   kDataN --> kData : LF
   kDataN --> kInvalid : *
   kDataN : ASSERT(size_ > 0)

   state kEndN <<SIZE_STATE>>
   kEndN --> kEnd : LF
   kEndN --> kInvalid : *

   kData : Consume size_\nbytes of input.
   kData --> kSizeR : Input consumed

   state kSizeR <<SIZE_STATE>>
   kSizeR --> kSizeN : CR
   kSizeR --> kInvalid : *

   state kSizeN <<SIZE_STATE>>
   kSizeN --> kSize : LF
   kSizeN --> kInvalid : *

   kInvalid --> [*]
   kEnd --> [*]
   @enduml
