.. _cacheurl-plugin:

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
request by using any portion of the url via regex. It is designed so that multiple requests that have different
URLs but the same content (for example, site mirrors) need be cached
only once.

Installation
============

This plugin is only built if the configure option ::

    --enable-experimental-plugins

is given at build time.

Configuration
=============

Create a ``cacheurl.config`` file in the plugin directory with the url
regex patterns to match. 

``url_pattern   cache_key_replacement``


The url_pattern is a regular expression (pcre). The replacement can contain $1, $2 and so on, which will be replaced with the appropriate matching group from the pattern.

Add the plugin to your :file:`plugin.config` file::

    cacheurl.so

Start traffic server. Any rewritten URLs will be written to
``cacheurl.log`` in the log directory by default.

Examples
========
1. To make files from s1.example.com, s2.example.com and s3.example.com all be cached with the same key. Adding a unique suffix (TSINTERNAL in this example) to the cache key guarantees that it won't clash with a real URL should s.example.com exist.

    ``http://s[123].example.com/(.*)  http://s.example.com.TSINTERNAL/$1``

2. Cache based on only some parts of a query string (e.g. ignore session information). This plucks out the id and format query string variables and only considers those when making the cache key.

    ``http://www.example.com/video\?.*?\&?(id=[0-9a-f]*).*?\&(format=[a-z]*) http://video-srv.example.com.ATSINTERNAL/$1&$2``

3. Completely ignore a query string for a specific page

    ``http://www.example.com/some/page.html(?:\?|$) http://www.example.com/some/page.html``

More docs
=============

There are some docs on cacheurl in Chinese, please find them in the following:

.. http://people.apache.org/~zym/trafficserver/cacheurl.html`` <http://people.apache.org/~zym/trafficserver/cacheurl.html>`_

https://blog.zymlinux.net/index.php/archives/195
