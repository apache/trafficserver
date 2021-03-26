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

.. _admin-plugins-rate-limit:

Rate Limit Plugin
********************

The :program:`rate_limit` plugin provides basic mechanism for how much
traffic a particular service (remap rule) is allowed to do. Currently,
the only implementation is a limit on how many active client transactions
a service can have. However, it would be easy to refactor this plugin to
allow for adding new limiter policies later on.

The limit counters and queues are per remap rule only, i.e. there is
(currently) no way to group transaction limits from different remap rules
into a single rate limiter.

All configuration is done via :file:`remap.config`, and the following options
are available:

.. program:: rate-limit

.. option:: --limit

   The maximum number of active client transactions.

.. option:: --queue

   When the limit (above) has been reached, all new transactions are placed
   on a FIFO queue. This option (optional) sets an upper bound on how many
   queued transactions we will allow. When this threshold is reached, all
   additional transactions are immediately served with an error message.

   The queue is effectively disabled if this is set to `0`, which implies
   that when the transaction limit is reached, we immediately start serving
   error responses.

   The default queue size is `UINT_MAX`, which is essentially unlimited.

.. option:: --error

   An optional HTTP status error code, to be used together with the
   :option:`--queue` option above. The default is `429`.

.. option:: --header

   This is an optional HTTP header name, which will be added to the client
   request header IF the transaction was delayed (queued). This can be useful
   to for example log the delays for later analysis.

   It is recommended that an `@` header is used here, e.g. `@RateLimit-Delay`,
   since this header will not leave the ATS server instance. The value here is
   appended to the header should one already exist.

Examples
--------

This example shows a simple rate limiting of `128` concurently active client
transactions, with a maximum queue size of `256`. The default of HTTP status
code `429` is used when queue is full.

    map http://cdn.example.com/ http://some-server.example.com \
      @plugin=rate_limit.so @pparam=--limit=128 @pparam=--queue=256


This example would put a hard transaction (in) limit to 256, with no backoff
queue:

    map http://cdn.example.com/ http://some-server.example.com \
      @plugin=rate_limit.so @pparam=--limit=256 @pparam=--queue=0 \
      @pparam=--header=@RateLimit-Delay
