CacheURL Plugin
***************

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



This plugin allows you to change the key that is used for caching a
request.

It is designed so that multiple requests that have different URLs but
the same content (for example, site mirrors) need be cached only once.

Installation
============

::
    make
    sudo make install

If you don't have the traffic server binaries in your path, then you
will need to specify the path to tsxs manually:

::
    make TSXS=/opt/ts/bin/tsxs
    sudo make TSXS=/opt/ts/bin/tsxs install

Configuration
=============

Create a ``cacheurl.config`` file in the plugin directory with the url
patterns to match. See the ``cacheurl.config.example`` file for what to
put in this file.

Add the plugin to your
```plugins.config`` <../../configuration-files/plugins.config>`_ file:

::
    cacheurl.so

Start traffic server. Any rewritten URLs will be written to
``cacheurl.log`` in the log directory by default.

