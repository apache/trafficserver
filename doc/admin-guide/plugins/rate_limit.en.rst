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

Remap Plugin
------------

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

.. option:: --retry

   An optional retry-after value, which if set will cause rejected (e.g. `429`)
   responses to also include a header `Retry-After`.

.. option:: --header

   This is an optional HTTP header name, which will be added to the client
   request header IF the transaction was delayed (queued). The value of the
   header is the delay, in milliseconds. This can be useful to for example
   log the delays for later analysis.

   It is recommended that an `@` header is used here, e.g. `@RateLimit-Delay`,
   since this header will not leave the ATS server instance.

.. option:: --maxage

   An optional `max-age` for how long a transaction can sit in the delay queue.
   The value (default 0) is the age in milliseconds.

Global Plugin
-------------

As a global plugin, the rate limiting currently applies only for TLS enabled
connections, based on the SNI from the TLS handshake. The basic use is as::

    rate_limit.so SNI=www1.example.com,www2.example.com --limit=2 --queue=2 --maxage=10000

.. Note::

    As a global plugin, it's highly recommended to also reduce the Keep-Alive inactive
    timeout for the service(s) controlled by this plugin. This avoids the risk of having
    idle connections consume too many of the available resources. This is easily
    done using e.g. the ``conf_remap`` plugin,
    :ts:cv:`proxy.config.http.keep_alive_no_activity_timeout_in`.

The following options are available:

.. program:: rate-limit

.. option:: --limit

   The maximum number of active client transactions.

.. option:: --queue

   When the limit (above) has been reached, all new connections are placed
   on a FIFO queue. This option (optional) sets an upper bound on how many
   queued transactions we will allow. When this threshold is reached, all
   additional connections are immediately errored out in the TLS handshake.

   The queue is effectively disabled if this is set to `0`, which implies
   that when the transaction limit is reached, we immediately start serving
   error responses.

   The default queue size is `UINT_MAX`, which is essentially unlimited.

.. option:: --maxage

   An optional `max-age` for how long a transaction can sit in the delay queue.
   The value (default 0) is the age in milliseconds.

Examples
--------

This example shows a simple rate limiting of `128` concurrently active client
transactions, with a maximum queue size of `256`. The default of HTTP status
code `429` is used when queue is full: ::

    map http://cdn.example.com/ http://some-server.example.com \
      @plugin=rate_limit.so @pparam=--limit=128 @pparam=--queue=256


This example would put a hard transaction (in) limit to 256, with no backoff
queue, and add a header with the transaction delay if it was queued: ::

    map http://cdn.example.com/ http://some-server.example.com \
      @plugin=rate_limit.so @pparam=--limit=256 @pparam=--queue=0 \
      @pparam=--header=@RateLimit-Delay

This final example will limit the active transaction, queue size, and also
add a `Retry-After` header once the queue is full and we return a `429` error: ::

    map http://cdn.example.com/ http://some-server.example.com \
      @plugin=rate_limit.so @pparam=--limit=256 @pparam=--queue=1024 \
      @pparam=--retry=3600 @pparam=--header=@RateLimit-Delay

In this case, the response would look like this when the queue is full: ::

    HTTP/1.1 429 Too Many Requests
    Date: Fri, 26 Mar 2021 22:42:38 GMT
    Connection: keep-alive
    Server: ATS/10.0.0
    Cache-Control: no-store
    Content-Type: text/html
    Content-Language: en
    Retry-After: 3600
    Content-Length: 207
