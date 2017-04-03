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

.. include:: ../../../../common.defs

Example: Query Remap Plugin
***************************

The sample remap plugin, ``query_remap.c``, maps client requests to a
number of servers based on a hash of the request's URL query parameter.
This can be useful for spreading load for a given type of request among
backend servers, while still maintaining "stickiness" to a single server
for similar requests. For example, a search engine may want to send
repeated queries for the same keywords to a server that has likely
cached the result from a prior query.

Configuration of query\_remap
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The query remap plugin will allow the query parameter name to be
specified, along with the hostnames of the servers to hash across.
Sample :file:`remap.config` rules using ``query_remap`` will look like::

    map http://www.example.com/search http://srch1.example.com/search @plugin=query_remap.so @pparam=q @pparam=srch1.example.com @pparam=srch2.example.com @pparam=srch3.example.com
    map http://www.example.com/profiles http://prof1.example.com/profiles @plugin=query_remap.so @pparam=user_id @pparam=prof1.example.com @pparam=prof2.example.com

The first :code:`@pparam` specifies the query param key for which the value
will be hashed. The remaining parameters list the hostnames of the
servers. A request for ``http://www.example.com/search?q=apache`` will
match the first rule. The plugin will look for the *``q``* parameter and
hash the value '``apache``\ ' to pick from among
``srch_[1-3]_.example.com`` to send the request.

If the request does not include a *``q``* query parameter and the plugin
decides not to modify the request, the default toURL
'``http://srch1.example.com/search``\ ' will be used by TS.

The parameters are passed to the plugin's ``tsremap_new_instance``
function. In ``query_remap``, ``tsremap_new_instance`` creates a
plugin-defined ``query_remap_info`` struct to store its configuration
parameters.

.. literalinclude:: ../../../../../example/query_remap/query_remap.c
  :language: c
  :lines: 37-42

The :code:`ihandle`, an opaque pointer that can be used to pass
per-instance data, is set to this struct pointer and will be passed to
the :code:`TSRemapDoRemap` function when it is triggered for a request.

.. literalinclude:: ../../../../../example/query_remap/query_remap.c
  :language: c
  :lines: 53-88

Another way remap plugins may want handle more complex configuration is
to specify a configuration filename as a ``pparam`` and parse the
specified file during instance initialization.

Performing the Remap
~~~~~~~~~~~~~~~~~~~~

The plugin implements the ``tsremap_remap`` function, which is called
when TS has read the client HTTP request headers and matched the request
to a remap rule configured for the plugin. The ``TSRemapRequestInfo``
struct contains input and output members for the remap operation.

``tsremap_remap`` uses the configuration information passed via the
:code:`ihandle` and checks the ``request_query`` for the configured query
parameter. If the parameter is found, the plugin sets a ``new_host`` to
modify the request host:

.. literalinclude:: ../../../../../example/query_remap/query_remap.c
  :language: c
  :lines: 112-166
