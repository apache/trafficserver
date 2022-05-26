.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. _admin-plugins-money-trace:


Money Trace Plugin
******************

Description
===========

This plugin allows ATS to participate in a distributed tracing system
based upon the Comcast "Money" distributed tracing and monitoring library.
The Comcast "Money" library has its roots in Google's Dapper and Twitters
Zipkin systems.  A money trace header or session id, is attached to
transaction and allows an operator with the appropriate logging systems
in place, to determine where errors and/or latency may exit.

Use of the library enables the tracing of a transaction through all
systems that participate in handling the request. See the documentation
on this open source library at https://github.com/Comcast/money.

How it Works
============

This plugin checks incoming requests for the "X-MoneyTrace" header.
If the header is not present no further processing takes place.
However if the header is present,  the plugin will check to see if the
request has been cached.  If so, the plugin will add the "X-Moneytrace"
header from the incoming request to the cached response returned to the
client as required by the money_trace protocol.  If the request has not
been cached, the plugin will extends the trace context by creating a new
"X-MoneyTrace" header for inclusion in the outgoing request to a parent
cache or origin server.  The extended header includes the 'trace-id'
from the incoming request, the incoming span-id becomes the outgoing
parent-id and the plugin generates a new span id for the
outgoing request using the current state machine id.

See the documentation at the link above for a complete description on
the "X-MoneyTrace" header and how to use and extend it in a distributed
tracing system.

A sample money-trace header:

::

  X-MoneyTrace: trace-id=aa234a23-189e-4cc4-98ed-b5327b1ec231-3;parent-id=0;span-id=4303691729133364974

Installation
============

The `Money Trace` plugin can be either a :term:`remap plugin` or
:term:`global plugin`.  Enable it by adding ``money_trace.so`` to your
:file:`remap.config` file or :file:`plugin.config`.

Here is an example remap.config entry:

::

  map http://vod.foobar.com http://origin.vod.foobar.com @plugin=money_trace.so

.. _MoneyTrace:    https://github.com/Comcast/money

Configuration
=============

The plugin supports the following options:

* ``--create-if-none=[true|false]`` (default: ``false``)

If no X-MoneyTrace header is found in the client request one will
be manufactured using the transaction UUID as trace-id,
the transaction state machine id as span-id and parent-id set to '0'.

* ``--global-skip-header=[header name]`` (default: null/disable)

This setting only applies to a :term:`global plugin` instance
and allows remap plugin instances to override :term:`global plugin`
behavior by disabling the :term:`global plugin`

Because a :term:`global plugin` runs before any :term:`remap plugin`
in the remap phase a pregen header may still be created by the
:term:`global plugin` if configured to do so.

The global skip check is performed during the post remap phase in order
to allow remap plugins (like `header rewrite`) to set this skip header.

It is strongly suggested to use a private ATS header (begins with '@')
as this value.

* ``--header=[header name]`` (default: ``X-MoneyTrace``)

Allows the money trace header to be overridden.

* ``--passthru=[true|false]`` (default: ``false``)

In this mode ATS acts transparently and passes the client money trace
header through to the parent.  It also returns this same header back to
the client.  This option ignores the --create-if-none setting.

* ``--pregen-header=[header name]`` (default: null/disable)

Normally the money trace header for a transaction is only added to the
transaction server request headers.  If this argument is supplied the
header will be generated earlier in the transaction and added to the
client request headers.  Use this for debug or for logging the current
transaction's money trace header.  It is suggested to use a private
ATS header (begins with a '@') for this value.  A :file:`logging.yaml`
entry with pregen-header=@MoneyTrace might look like:

::

  %<{@MoneyTrace}cqh>

Robustness
==========

This plugin tries to be robust in its parsing.  At a minimum the value
must start with `trace-id=` set to a none empty value.

If `span-id=` is found in the header value that will be used as the
parent-id for an upstream request.  Otherwise '0' will be its value.

If the incoming money trace header is invalid, it is handled based
on the --create-if-none setting.  If create-if-none is set a new
money trace header will be generated and used.  Otherwise the
incoming client header value will be passed through.

