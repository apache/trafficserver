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

.. include:: ../../common.defs

.. _developer-debug-tags:

Debug Tags
**********

Use the API
``void TSDebug (const char *tag, const char *format_str, ...)`` to add
traces in your plugin. In this API:

-  ``tag`` is the Traffic Server parameter that enables Traffic Server
   to print out *``format_str``*

-  ``...`` are variables for *``format_str``* in the standard ``printf``
   style.

Run Traffic Server with the ``-Ttag`` option. For example, if the tag is
``my-plugin``, then the debug output goes to ``traffic.out.``\ See
below:

::

       traffic_server -T"my-plugin"

Set the following variables in :file:`records.config` (in the Traffic Server
``config`` directory):

::

       CONFIG proxy.config.diags.debug.enabled INT 1
       CONFIG proxy.config.diags.debug.tags STRING debug-tag-name

In this case, debug output goes to ``traffic.out``.

Example:

.. code-block:: c

       TSDebug ("my-plugin", "Starting my-plugin at %d", the_time);

The statement ``"Starting my-plugin at <time>"`` appears whenever you
run Traffic Server with the ``my-plugin`` tag:

::

       traffic_server -T"my-plugin"

Other Useful Internal Debug Tags
================================

Embedded in the base Traffic Server code are many debug tags for
internal debugging purposes. These can also be used to follow Traffic
Server behavior for testing and analysis.

The debug tag setting (``-T`` and ``proxy.config.diags.debug.tags``) is a
anchored regular expression against which the tag for a specific debug
message is matched. This means the value "http" matches debug emssages
with the tag "http" but also "http\_seq" and "http\_trans". If you want
multiple tags then use a pipe symbol to separate the tags. For example
"http\_tproxy\|dns\|hostdb" will match any of the message tags
"http\_tproxy", "dns", "hostdb", or "dns\_srv" (but not "http\_trans"
nor "splitdns").

Some of the useful HTTP debug tags are:

-  ``http_hdrs`` - traces all incoming and outgoing HTTP headers.

-  ``http_trans`` - traces actions in an HTTP transaction.

-  ``http_seq`` - traces the sequence of the HTTP state machine.

-  ``http_tproxy`` - transparency related HTTP events

-  ``dns`` - DNS operations

-  ``hostdb`` - Host name lookup

-  ``iocore_net`` - Socket and low level IO (very voluminous)

-  ``socket`` - socket operations

-  ``ssl`` - SSL related events

-  ``cache`` - Cache operations (many subtags, examine the output to
   narrow the tag set)

-  ``cache_update`` - Cache updates including writes

-  ``cache_read`` - Cache read events.

-  ``dir_probe`` - Cache searches.

-  ``sdk`` - gives some warning concerning API usage.


