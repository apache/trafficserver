.. _admin-plugins-cachekey:

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


Cache Key Manipulation Plugin
*****************************

Description
===========

This plugin allows some common cache key manipulations based on various HTTP request components.  It can

* sort query parameters to prevent query parameters reordereding from being a cache miss
* ignore specific query parameters from the cache key by name or regular expression
* ignore all query parameters from the cache key
* only use specific query parameters in the cache key by name or regular expression
* include headers or cookies by name
* capture values from the ``User-Agent`` header.
* classify request using ``User-Agent`` and a list of regular expressions

Plugin parameters
=================

All parameters are optional, and if not used, their default values are as mentioned below. Boolean values default to ``false`` and the rest default to an empty list. Examples of each parameter's usage can be found below.

* URI query parameters
    * If no query related plugin parameters are used, the query is included as received from the UA in the cache key.
    * ``--exclude-params`` (default: empty list) - comma-separated list of query params to be black-listed in the cache key. If the list is empty then no black-list is applied (no query parameters will be excluded from the cache key). The exclude list overrides the include list.
    * ``--include-params`` (default: empty list) - comma-separated list of query params to be white-listed in the cache key. If the list is empty then no white-list is applied (all query parameters will be included in the cache key).
    * ``--include-match-params`` (default: empty list) - regular expression matching query parameter names which will be white-listed in the cache key.
    * ``--exclude-match-params`` (default: empty list) - regular expression matching query parameter names which will be black-listed in the cache key.
    * ``--remove-all-params`` (boolean:``true|false``, ``0|1``, ``yes|no``, default: ``false``) - if equals ``true`` then all query parameters are removed (the whole query string) and all other URI query parameter related settings (if used) will have no effect.
    * ``--sort-params`` (boolean:``true|false``, ``0|1``, ``yes|no``, default: ``false``) - if equals ``true`` then all query parameters are sorted in an increasing case-sensitive order
* HTTP headers
    * ``--include-headers`` (default: empty list) - comma separated list of headers to be added to the cache key.
* HTTP cookies
    * ``--include-cookies`` (default: empty list) - comma separated list of cookies to be added to the cache key.

* Host name, port and custom prefix
    * Host and port are added to the beginning of the cache key by default unless a custom preffix by using ``--static-prefix`` or ``--capture-prefix`` plugin parameters is specified.
    * ``--static-prefix`` (default: empty string) - if specified and not an empty string the value will be added to the beginning of the cache key.
    * ``--capture-prefix=<capture_definition>`` (default: empty string) - if specified and not an empty string will capture strings from ``host:port`` based on the ``<capture_definition>`` (see below) and add them to the beginning of the cache key.
    * If ``--static-prefix`` and ``--capture-prefix`` are used together then the value of ``--static-prefix`` is added first to the cache key, followed by the ``--capture-prefix`` capturing/replacement results.

* ``User-Agent`` classification
    * ``--ua-whitelist=<classname>:<filename>`` (default: empty string) - loads a regex patterns list from a file ``<filename>``, the patterns are matched against the ``User-Agent`` header and if matched ``<classname>`` is added it to the key.
    * ``--ua-blacklist=<classname>:<filename>`` (default: empty string) - loads a regex patterns list from a file ``<filename>``, the patterns are matched against the ``User-Agent`` header and if **not** matched ``<classname>`` is added it to the key.

* ``User-Agent`` regex capturing and replacement
    * ``--ua-capture=<capture_definition>`` (default: empty string) - if specified and not an empty string will capture strings from ``User-Agent`` header based on ``<capture_definition>`` (see below) and will add them to the cache key.

* ``<capture_definition>`` can be in the following formats
    * ``<regex>`` - ``<regex>`` defines regex capturing groups, up to 10 captured strings based on ``<regex>`` will be added to the cache key.
    * ``/<regex>/<replacement>/`` - ``<regex>`` defines regex capturing groups, ``<replacement>`` defines a pattern where the captured strings referenced with ``$0`` ... ``$9`` will be substituted and the result will be added to the cache key.

Cache Key Structure
===================

::

               |                           hierarchical part                                    query
  HTTP request | ┌────────────────────────────────┴─────────────────────────────────────────┐┌────┴─────┐
  components   |   URI host and port       HTTP headers and cookies               URI path    URI query
               | ┌────────┴────────┐┌────────────────┴─────────────────────────┐┌─────┴─────┐┌────┴─────┐
  Sample 1     | /www.example.com/80/popular/Mozilla/5.0/H1:v1/H2:v2/C1=v1;C2=v2/path/to/data?a=1&b=2&c=3
  Sample 2     | /nice_custom_prefix/popular/Mozilla/5.0/H1:v1/H2:v2/C1=v1;C2=v2/path/to/data?a=1&b=2&c=3
               | └────────┬────────┘└───┬──┘└─────┬────┘└────┬─────┘└─────┬────┘└─────┬─────┘└────┬─────┘
  Cache Key    |     host:port or   UA-class UA-captures   headers     cookies       path       query
  components   |     custom prefix           replacement


* With the current implementation the following cache key components are always present in the cache key:
    * ``prefix or host:port`` - included at the beginning of the cache key. If neither ``--static-prefix`` nor ``--capture-prefix`` are specified or are empty strings then ``host:port`` from the request URI are used.
    * ``path`` - URI path included **as is** (but can be empty)
* The rest of the cache key components are optional and their presence in the cache key depends on the plugin configuration and the HTTP requests handled by the plugin:
    * ``UA-class`` - a single class name, result of UA classification defined by ``--ua-whitelist`` and ``--ua-blacklist`` parameters.
    * ``UA-captures`` - a result of the regex capture (and possibly replacement) from the first ``User-Agent`` header.
    * ``headers`` - always sorted list of headers defined by ``--include-headers``
    * ``cookies`` - always sorted list of headers defined by ``--include-cookies``
    * ``query`` - the request URI query **as is** or a list of query parameters proccessed by this plugin as configured.
* The following URI components are ignored (not included in the cache key):
    * ``scheme:``
    * ``user:password@`` from the ``authority`` URI component
    * ``#fragment``

The following is an example of how the above sample keys were generated (``Sample 1`` and ``Sample 2``).

Traffic server configuration ::

  $ cat etc/trafficserver/remap.config
  map http://www.example.com http://www.origin.com \
      @plugin=cachekey.so \
          @pparam=--ua-whitelist=popular:popular_agents.config \
          @pparam=--ua-capture=(Mozilla\/[^\s]*).* \
          @pparam=--include-headers=H1,H2 \
          @pparam=--include-cookies=C1,C2 \
          @pparam=--include-params=a,b,c \
          @pparam=--sort-params=true

  $ cat etc/trafficserver/popular_agents.config
  ^Mozilla.*
  ^Twitter.*
  ^Facebo.*

  $ cat etc/trafficserver/plugin.config
  xdebug.so

HTTP request ::

  $ curl 'http://www.example.com/path/to/data?c=3&a=1&b=2&x=1&y=2&z=3' \
      -v -x 127.0.0.1:8080 -o /dev/null -s \
      -H "H1: v1" \
      -H "H2: v2" \
      -H "Cookie: C1=v1; C2=v2" \
      -H 'User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A' \
      -H 'X-Debug: X-Cache-Key'
  * About to connect() to proxy 127.0.0.1 port 8080 (#0)
  *   Trying 127.0.0.1... connected
  * Connected to 127.0.0.1 (127.0.0.1) port 8080 (#0)
  > GET http://www.example.com/path/to/data?c=3&a=1&b=2&x=1&y=2&z=3 HTTP/1.1
  > Host: www.example.com
  > Accept: */*
  > Proxy-Connection: Keep-Alive
  > H1: v1
  > H2: v2
  > Cookie: C1=v1; C2=v2
  > User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.75.14 (KHTML, like Gecko) Version/7.0.3 Safari/7046A194A
  > X-Debug: X-Cache-Key
  >
  < HTTP/1.1 200 OK
  < Server: ATS/6.1.0
  < Date: Thu, 19 Nov 2015 23:17:58 GMT
  < Content-type: application/json
  < Age: 0
  < Transfer-Encoding: chunked
  < Proxy-Connection: keep-alive
  < X-Cache-Key: /www.example.com/80/popular/Mozilla/5.0/H1:v1/H2:v2/C1=v1;C2=v2/path/to/data?a=1&b=2&c=3
  <
  { [data not shown]
  * Connection #0 to host 127.0.0.1 left intact
  * Closing connection #0

The response header ``X-Cache-Key`` header contains the cache key: ::

  /www.example.com/80/popular/Mozilla/5.0/H1:v1/H2:v2/C1=v1;C2=v2/path/to/data?a=1&b=2&c=3

The ``xdebug.so`` plugin and ``X-Debug`` request header are used just to demonstrate basic cache key troubleshooting.

If we add ``--static-prefix=nice_custom_prefix`` to the remap rule then the cache key would look like the following: ::

  /nice_custom_prefix/popular/Mozilla/5.0/H1:v1/H2:v2/C1=v1;C2=v2/path/to/data?a=1&b=2&c=3

Usage examples
==============

URI query parameters
^^^^^^^^^^^^^^^^^^^^

Ignore the query string (all query parameters)
""""""""""""""""""""""""""""""""""""""""""""""
The following added to the remap rule will ignore the query, removing it from the cache key. ::

  @plugin=cachekey.so @pparam=--remove-all-params=true

Cache key normalization by sorting the query parameters
"""""""""""""""""""""""""""""""""""""""""""""""""""""""
The following will normalize the cache key by sorting the query parameters. ::

  @plugin=cachekey.so @pparam=--sort-params=true

If the URI has the following query string ``c=1&a=1&b=2&x=1&k=1&u=1&y=1`` the cache key will use ``a=1&b=2&c=1&k=1&u=1&x=1&y=1``

Ignore (exclude) certain query parameters
"""""""""""""""""""""""""""""""""""""""""

The following will make sure query parameters `a` and `b` will **not** be used when constructing the cache key. ::

  @plugin=cachekey.so @pparam=--exclude-params=a,b

If the URI has the following query string ``c=1&a=1&b=2&x=1&k=1&u=1&y=1`` the cache key will use ``c=1&x=1&k=1&u=1&y=1``

Ignore (exclude) certain query parameters from the cache key by using regular expression (PCRE)
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
The following will make sure query parameters ``a`` and ``b`` will **not** be used when constructing the cache key. ::

  @plugin=cachekey.so @pparam=--exclude-match-params=(a|b)

If the URI has the following query string ``c=1&a=1&b=2&x=1&k=1&u=1&y=1`` the cache key will use ``c=1&x=1&k=1&u=1&y=1``

Include only certain query parameters
"""""""""""""""""""""""""""""""""""""
The following will make sure only query parameters `a` and `c` will be used when constructing the cache key and the rest will be ignored. ::

  @plugin=cachekey.so @pparam=--include-params=a,c

If the URI has the following query string ``c=1&a=1&b=2&x=1&k=1&u=1&y=1`` the cache key will use ``c=1&a=1``

Include only certain query parameters by using regular expression (PCRE)
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
The following will make sure only query parameters ``a`` and ``c`` will be used when constructing the cache key and the rest will be ignored. ::

  @plugin=cachekey.so @pparam=--include-match-params=(a|c)

If the URI has the following query string ``c=1&a=1&b=2&x=1&k=1&u=1&y=1`` the cache key will use ``c=1&a=1``

White-list + black-list certain parameters using multiple parameters in the same remap rule.
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameters in the remap rule: ::

  @plugin=cachekey.so \
      @pparam=--exclude-params=x \
      @pparam=--exclude-params=y \
      @pparam=--exclude-params=z \
      @pparam=--include-params=y,c \
      @pparam=--include-params=x,b

and if the URI has the following query string ``c=1&a=1&b=2&x=1&k=1&u=1&y=1`` the cache key will use ``c=1&b=1``

White-list + black-list certain parameters using multiple parameters in the same remap rule and regular expressions (PCRE).
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameters in the remap rule: ::

  @plugin=cachekey.so \
      @pparam=--exclude-match-params=x \
      @pparam=--exclude-match-params=y \
      @pparam=--exclude-match-params=z \
      @pparam=--include-match-params=(y|c) \
      @pparam=--include-match-params=(x|b)

and if the URI has the following query string ``c=1&a=1&b=2&x=1&k=1&u=1&y=1`` the cache key will use ``c=1&b=1``

Mixing --include-params, --exclude-params, --include-match-param and --exclude-match-param
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameters in the remap rule: ::

  @plugin=cachekey.so \
      @pparam=--exclude-params=x \
      @pparam=--exclude-match-params=y \
      @pparam=--exclude-match-params=z \
      @pparam=--include-params=y,c \
      @pparam=--include-match-params=(x|b)

and if the URI has the following query string ``c=1&a=1&b=2&x=1&k=1&u=1&y=1`` the cache key will use ``c=1&b=1``

HTTP Headers
^^^^^^^^^^^^

Include certain headers in the cache key
""""""""""""""""""""""""""""""""""""""""
The following headers ``HeaderA`` and ``HeaderB`` will be used when constructing the cache key and the rest will be ignored. ::

  @plugin=cachekey.so @pparam=--include-headers=HeaderA,HeaderB

HTTP Cookies
^^^^^^^^^^^^

Include certain cookies in the cache key
""""""""""""""""""""""""""""""""""""""""

The following headers ``CookieA`` and ``CookieB`` will be used when constructing the cache key and the rest will be ignored. ::

  @plugin=cachekey.so @pparam=--include-headers=CookieA,CookieB


Host name, port and static prefix
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Replacing host:port with a static cache key prefix
"""""""""""""""""""""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameter in the remap rule. ::

  @plugin=cachekey.so @pparam=--static-prefix=static_prefix

the cache key will be prefixed with ``/static_prefix`` instead of ``host:port`` when ``--static-prefix`` is not used.

Capturing from the host:port and adding it to beginning of cache key prefix
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameter in the remap rule. ::

  @plugin=cachekey.so @pparam=--capture-prefix=(test_prefix).*:([^\s\/$]*)

the cache key will be prefixed with ``/test_prefix/80`` instead of ``test_prefix_371.example.com:80`` when ``--capture-prefix`` is not used.

Combining --static-prefix and --capture-prefix
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameter in the remap rule. ::

  @plugin=cachekey.so @pparam=--capture-prefix=(test_prefix).*:([^\s\/$]*) @pparam=--static-prefix=static_prefix

the cache key will be prefixed with ``/static_prefix/test_prefix/80`` instead of ``test_prefix_371.example.com:80`` when neither ``--capture-prefix`` nor ``--static-prefix`` are used.

User-Agent capturing, replacement and classification
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Let us say we have a request with ``User-Agent`` header: ::

  Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3)
  AppleWebKit/537.75.14 (KHTML, like Gecko)
  Version/7.0.3 Safari/7046A194A


Capture PCRE groups from User-Agent header
""""""""""""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameter::

  @plugin=cachekey.so \
      @pparam=--ua-capture=(Mozilla\/[^\s]*).*(AppleWebKit\/[^\s]*)

then ``Mozilla/5.0`` and ``AppleWebKit/537.75.14`` will be used when constructing the key.

Capture and replace groups from User-Agent header
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
If the plugin is used with the following plugin parameter::

  @plugin=cachekey.so \
      @pparam=--ua-capture=/(Mozilla\/[^\s]*).*(AppleWebKit\/[^\s]*)/$1_$2/

then ``Mozilla/5.0_AppleWebKit/537.75.14`` will be used when constructing the key.

User-Agent white-list classifier
""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameter::

  @plugin=cachekey.so \
      @pparam=--ua-whitelist=browser:browser_agents.config

and if ``browser_agents.config`` contains: ::

  ^Mozilla.*
  ^Twitter.*
  ^Facebo.*

then ``browser`` will be used when constructing the key.

User-Agent black-list classifier
""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameter::

  @plugin=cachekey.so \
      @pparam=--ua-blacklist=browser:tool_agents.config

and if ``tool_agents.config`` contains: ::

  ^PHP.*
  ^Python.*
  ^curl.*

then ``browser`` will be used when constructing the key.
