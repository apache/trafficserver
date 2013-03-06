:title: HIPES system (undocumented)
:title: Apache Traffic Server Plugins

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


This plugin implements the HIPES system.

Configuration # {#Configuration}
================================

``server:<host>``
    Name of the server to send this request to

``urlp:<name>``
    Default: ``url``
    Name of the query parameter for the service URL

``path:<path>``
    Default: ``/``
    Path to use for the service URL

``ssl``
    Default: ``no``
    Use SSL when connecting to the service

``service``
    Service server, ``host[:port]``

``server``
    Default: ``hipes.yimg.com``
    Name of HIPES server, ``host[:port]``

``active_timeout``
    The active connection timeout in ms

``no_activity_timeout``
    The no activity timeout in ms

``connect_timeout``
    The connect timeout in ms

``dns_timeout``
    The DNS timeout

The timeout options override the server defaults (from
```records.config`` <../../configuration-files/records.config>`_), and
only apply to the connection to the specific "service" host.

