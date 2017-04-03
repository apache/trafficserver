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
==================

This is a remap plugin  that allows ATS to participate in a distrbuted tracing system based upon
the Comcast "Money" distributed tracing and monitoring library.  The Comcast "Money" library has
its roots in Google's Dapper and Twitters Zipkin systems.  A money trace header or session id, is
attached to transaction and allows an operator with the appropriate logging systems in place,
to determine where errors and/or latency may exit.

Use of the library enables the tracing of a transaction through all systems that participate in
handling the request. See the documentation on this open source library at
https://github.com/Comcast/money.

How it Works
------------

This plugin checks incoming requests for the "X-MoneyTrace" header.  If the header is not present
no further processing takes place.  However if the header is present,  the plugin will check to
to see if the request has been cached.  If so, the plugin will add the "X-Moneytrace" header from the
incoming request to the cached response returned to the client as required by the money_trace
protocol.  If the request has not been cached, the plugin will extends the trace context by creating a new
"X-MoneyTrace" header for inclusion in the outgoing request to a parent cache or origin server.
The extended header includes the 'trace-id' from the incoming request, the incoming span-id
becomes the outgoing parent-id and the plugin generates a new random long span id for the outgoing request.
See the documentation at the link above for a complete description on the "X-MoneyTrace" header and how
to use and extend it in a distributed tracing system.

Installation
------------

The `Money Trace` plugin is a :term:`remap plugin`.  Enable it by adding
``money_trace.so`` to your :file:`remap.config` file.  There are no options.

Here is an example remap.config entry:

::

  map http://vod.foobar.com http://origin.vod.foobar.com @plugin=money_trace.so

.. _MoneyTrace:    https://github.com/Comcast/money
