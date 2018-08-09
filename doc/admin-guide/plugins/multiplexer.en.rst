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

|Name| is a remap plug-in that allows a request to be multiplexed one or more times and sent to
different remap entries. Both headers and body (in case of POST or PUT methods, only) are copied
into the new requests.

Description
===========

Actions:

#. Adds ``X-Multiplexer: original`` header into client's request.
#. Copies client's request (bodies are copied by transforming the request)
#. Changes ``Host`` header of the copy according to ``pparam`` from the remap rule.
#. Changes ``X-Multiplexer header`` to "copy".
#. Sends the copied request with :code:`TSHttpConnect`.

|Name| dispatches the request in background without blocking the original request. Multiplexed
responses are drained and discarded.

A global timeout can be overwritten through ``multiplexer__timeout`` environment variable representing how many nanoseconds to wait. A default 1s timeout is hard-coded.

Please use ``multiplexer`` tag for debugging purposes. While debugging, multiplexed requests and
responses are printed into the logs.

Multiplexer produces the following statistics consumed with ``traffic_ctl``:

*  failures: number of failed multiplexed requests
*  hits: number of successful multiplexed requests
*  requests: total number of multiplexed requests
*  time(avg): average time taken between multiplexed requests and their responses
*  timeouts: number of multiplexed requests which timed-out
*  size(avg): average size of multiplexed responses

Example remap.config::

    map http://www.example.com/a http://www.example.com/ @plugin=multiplexer.so @pparam=host1.example.com
    map http://www.example.com/b http://www.example.com/ @plugin=multiplexer.so @pparam=host2.example.com
    map http://www.example.com/c http://www.example.com/ @plugin=multiplexer.so @pparam=host1.example.com @pparam=host2.example.com


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

Notes
=====

.. note:: This should be moved to the |TS| documentation.
