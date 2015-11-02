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

.. _developer-plugins-examples-query-remap:

Query Remap Plugin
******************

.. toctree::
   :maxdepth: 2

   example-query-remap.en

The Remap plugin provides a more flexible, dynamic way of specifying
remap rules. It is not built on top of the Traffic Server APIs and
exists solely for the purpose of URL remapping. The remap plugin is not
global --it is configured on a per-remap rule basis, which enables you
to customize how URLs are redirected based on individual rules in the
``remap.config`` file.

The Traffic Server Remap API enables a plugin to dynamically map a
client request to a target URL. Each plugin is associated with one or
more remap rules in ``remap.config`` (an "instance"). If a request URL
matches a remap rule's "fromURL", then Traffic Server calls the
plugin-defined remap function for that request.

((Editor's note: additional text TBD; text in this chapter is still
under development))

Remap Header File
=================

The ``remap.h`` header file contains the Traffic Server remap API. By
default, the header file location is: ``/usr/local/include/ts/remap.h``

Required Functions
==================

A remap plugin is required to implement the following functions:

-  `TSRemapInit <http://people.apache.org/~amc/ats/doc/html/remap_8h.html#af7e9b1eee1c38c6f8dcc67a65ba02c24>`_:
   the remap initialization function, called once when the plugin is
   loaded

-  `TSRemapNewInstance <http://people.apache.org/~amc/ats/doc/html/remap_8h.html#a963de3eeed2ed7a2da483acf77dc42ca>`_:
   a new instance is created for each rule associated with the plugin.
   Called each time the plugin used in a remap rule (this function is
   what processes the pparam values)

-  `TSRemapDoRemap <http://people.apache.org/~amc/ats/doc/html/remap_8h.html#acf73f0355c591e145398211b3c0596fe>`_:
   the entry point used by Traffic Server to find the new URL to which
   it remaps; called every time a request comes in

Configuration
~~~~~~~~~~~~~

To associate a remap plugin with a remap rule, use the ``@plugin``
parameter. See the Admin Guide section (?TBD?) for details on
configuring remap plugins
