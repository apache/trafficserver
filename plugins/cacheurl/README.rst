:title: CacheURL Plugin

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

This plugin allows you to change the key that is used for caching a request.
It is designed so that multiple requests that have different URLs but the same
content (for example, site mirrors) need be cached only once.

Installation # {#Installation}
==============================

::
    make
    sudo make install

If you don't have the traffic server binaries in your path, then you will need
to specify the path to tsxs manually:

::
    make TSXS=/opt/ts/bin/tsxs
    sudo make TSXS=/opt/ts/bin/tsxs install

# Configuration # {#Configuration}
==================================

Add the plugin to your plugins.conf file: `cacheurl.so`

If you wish, you can specify a location for the cacheurl configuration file
by adding it as a parameter in plugins.conf. For example:

::
    cacheurl.so /etc/trafficserver/cacheurl.config

The default location for the config file is `cacheurl.config` in the plugins
directory.

Cacheurl can also be called as a remap plugin in `remap.config`. For example:

::
    map http://www.example.com/ http://origin.example.com/ @plugin=cacheurl.so @pparam=/path/to/cacheurl.config

Next, create the configuration file with the url patterns to match.

The configration file format is: `url_pattern cache_key_replacement`

The url_pattern is a regular expression (pcre). The replacement can contain
$1, $2 and so on, which will be replaced with the appropriate matching group
from the pattern.

Examples:

::
    # Make files from s1.example.com, s2.example.com and s3.example.com all
    # be cached with the same key.
    # Adding a unique suffix (TSINTERNAL in this example) to the cache key
    # guarantees that it won't clash with a real URL should s.example.com
    # exist.
    http://s[123].example.com/(.*)  http://s.example.com.TSINTERNAL/$1

    # Cache based on only some parts of a query string (e.g. ignore session
    # information). This plucks out the id and format query string variables and
    # only considers those when making the cache key.
    http://www.example.com/video\?.*?\&?(id=[0-9a-f]*).*?\&(format=[a-z]*) http://video-srv.example.com.ATSINTERNAL/$1&$2

    # Completely ignore a query string for a specific page
    http://www.example.com/some/page.html(?:\?|$) http://www.example.com/some/page.html

Start traffic server. Any rewritten URLs will be written to cacheurl.log in
the log directory by default.

.. vim: ft=rst
