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

.. _developer-plugins-examples-denylist-code:

Sample Source Code
******************

.. _denylist-1.c:

denylist_1.c
-------------

The sample denylisting plugin included in the Traffic Server SDK is
``denylist_1.c``. This plugin checks every incoming HTTP client request
against a list of web sites. If the client requests a
listed site, then the plugin returns an ``Access forbidden``
message to the client.

This plugin illustrates:

-  An HTTP transaction extension

-  How to examine HTTP request headers

-  How to use the logging interface

-  How to use the plugin configuration management interface

.. literalinclude:: ../../../../../example/plugins/c-api/denylist_1/denylist_1.c
   :language: c
