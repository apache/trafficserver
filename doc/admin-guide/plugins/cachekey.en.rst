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

.. _admin-plugins-cachekey:


Cache Key Manipulation Plugin
*****************************

Description
===========

This plugin allows some common cache key manipulations based on various HTTP request components.  It can

* sort query parameters to prevent query parameter reordering being a cache miss
* ignore specific query parameters from the cache key by name or regular expression
* ignore all query parameters from the cache key
* only use specific query parameters in the cache key by name or regular expression
* include headers or cookies by name
* capture values from the ``User-Agent`` header.
* classify request using ``User-Agent`` and a list of regular expressions
* capture and replace strings from the URI and include them in the cache key
* do more - please find more examples below.

URI type
========

The plugin manipulates the ``remap`` URI (value set during URI remap) by default. If manipulation needs to be based on the ``pristine`` URI (the value before URI remapping takes place) one could use the following option:

* ``--uri-type=[remap|pristine]`` (default: ``remap``)

Cache key structure and related plugin parameters
=================================================

::

                                hierarchical part                               query
  ┌───────────────────────────────────┴────────────────────────────────────┐┌─────┴──────┐
  ┌─────────────┬──────────────┬──────────────┬──────────────┬─────────────┬─────────────┐
  │  Prefix     |  User-Agent  │  Headers     │  Cookies     │  Path       │  Query      │
  │  section    |  section     │  section     │  section     │  section    │  section    │
  │  (default)  |  (optional)  │  (optional)  │  (optional)  │  (default)  │  (default)  │
  └─────────────┴──────────────┴──────────────┴──────────────┴─────────────┴─────────────┘

* The cache key set by the cachekey plugin can be considered as devided into several sections.
* Every section is manipulated separately by the related plugin parameters (more info in each section description below).
* "User-Agent", "Headers" and "Cookies" sections are optional and will be missing from the cache key if no related plugin parameters are used.
* "Prefix", "Path" and "Query" sections always have default values even if no related plugin parameters are used.
* All cachekey plugin parameters are optional and if missing some of the cache key sections will be missing (the optional sections) or their values will be left to their defaults.

"Prefix" section
^^^^^^^^^^^^^^^^

::

  Optional components      | ┌─────────────────┬──────────────────┬──────────────────────┐
  (included in this order) | │ --static-prefix | --capture-prefix │ --capture-prefix-uri │
                           | ├─────────────────┴──────────────────┴──────────────────────┤
  Default values if no     | │ /host/port                                                |
  optional components      | └───────────────────────────────────────────────────────────┘
  configured               |

* ``--static-prefix=<value>`` (default: empty string) - if specified and not an empty string the ``<value>`` will be added to the cache key.
* ``--capture-prefix=<capture_definition>`` (default: empty string) - if specified and not empty then strings are captured from ``host:port`` based on the ``<capture_definition>`` and are added to the cache key.
* ``--capture-prefix-uri=<capture_definition>`` (default: empty string) - if specified and not empty then strings are captured from the entire URI based on the ``<capture_definition>`` and are added to the cache key.
* If any of the "Prefix" related plugin parameters are used together in the plugin configuration they are added to the cache key in the order shown in the diagram.
* ``--remove-prefix=<true|false|yes|no|0|1`` (default: false) - if specified the prefix elements (host, port) are not processed nor appended to the cachekey. All prefix related plugin paramenters are ignored if this parameter is ``true``, ``yes`` or ``1``.



"User-Agent" section
^^^^^^^^^^^^^^^^^^^^

::

  Optional components      | ┌────────────┬──────────────┐
  (included in this order) | │ --ua-class | --ua-capture │
                           | ├────────────┴──────────────┤
  Default values if no     | │ (empty)                   |
  optional components      | └───────────────────────────┘
  configured               |

* ``User-Agent`` classification
    * ``--ua-whitelist=<classname>:<filename>`` (default: empty string) - loads a regex patterns list from a file ``<filename>``, the patterns are matched against the ``User-Agent`` header and if matched ``<classname>`` is added it to the key.
    * ``--ua-blacklist=<classname>:<filename>`` (default: empty string) - loads a regex patterns list from a file ``<filename>``, the patterns are matched against the ``User-Agent`` header and if **not** matched ``<classname>`` is added it to the key.
    * Multiple ``--ua-whitelist`` and ``--ua-blacklist`` can be used and the result will be defined by their order in the plugin configuration.
* ``User-Agent`` regex capturing and replacement
    * ``--ua-capture=<capture_definition>`` (default: empty string) - if specified and not empty then strings are captured from the ``User-Agent`` header based on ``<capture_definition>`` (see below) and are added to the cache key.
* If any ``User-Agent`` classification and regex capturing and replacement plugin parameters are used together they are added to the cache key in the order shown in the diagram.

"Headers" section
^^^^^^^^^^^^^^^^^

::

  Optional components      | ┌───────────────────┬───────────────────┐
                           | │ --include-headers │  --capture-header │
                           | ├───────────────────────────────────────┤
  Default values if no     | │ (empty)           |  (empty)          |
  optional components      | └───────────────────┴───────────────────┘
  configured               |

* ``--include-headers`` (default: empty list) - comma separated list of headers to be added to the cache key. The list of headers defined by ``--include-headers`` are always sorted before adding them to the cache key.

* ``--capture-header=<headername>:<capture_definition>`` (default: empty) - captures elements from header <headername> using <capture_definition> and adds them to the cache key.

"Cookies" section
^^^^^^^^^^^^^^^^^

::

  Optional components      | ┌───────────────────┐
                           | │ --include-cookies │
                           | ├───────────────────┤
  Default values if no     | │ (empty)           |
  optional components      | └───────────────────┘
  configured               |

* ``--include-cookies`` (default: empty list) - comma separated list of cookies to be added to the cache key. The list of cookies defined by ``--include-cookies`` are always sorted before adding them to the cache key.

"Path" section
^^^^^^^^^^^^^^

::

  Optional components      | ┌────────────────────┬────────────────┐
  (included in this order) | │ --capture-path-uri | --capture-path │
                           | ├────────────────────┴────────────────┤
  Default values if no     | │ URI path                            |
  optional components      | └─────────────────────────────────────┘
  configured               |

* if no path related plugin parameters are used, the URI path string is included in the cache key.
* ``--capture-path=<capture_definition>`` (default: empty string) - if specified and not empty then strings are captured from URI path based on the ``<capture_definition>`` and are added to the cache key.
* ``--capture-path-uri=<capture_definition>`` (default: empty string) - if specified and not empty then strings are captured from the entire URI based on the ``<capture_definition>`` and are added to the cache key.
* ``--remove-path=<true|false|yes|no|0|1`` (default: false) - if specified the HTTP URI path element is not processed nor appended to the cachekey. All path related plugin paramenters are ignored if this parameter is ``true``, ``yes`` or ``1``.

"Query" section
^^^^^^^^^^^^^^^

* If no query related plugin parameters are used, the query string is included in the cache key.
* ``--exclude-params`` (default: empty list) - comma-separated list of query params to be black-listed in the cache key. If the list is empty then no black-list is applied (no query parameters will be excluded from the cache key). The exclude list overrides the include list.
* ``--include-params`` (default: empty list) - comma-separated list of query params to be white-listed in the cache key. If the list is empty then no white-list is applied (all query parameters will be included in the cache key).
* ``--include-match-params`` (default: empty list) - regular expression matching query parameter names which will be white-listed in the cache key.
* ``--exclude-match-params`` (default: empty list) - regular expression matching query parameter names which will be black-listed in the cache key.
* ``--remove-all-params`` (boolean:``true|false``, ``0|1``, ``yes|no``, default: ``false``) - if equals ``true`` then all query parameters are removed (the whole query string) and all other URI query parameter related settings (if used) will have no effect.
* ``--sort-params`` (boolean:``true|false``, ``0|1``, ``yes|no``, default: ``false``) - if equals ``true`` then all query parameters are sorted in an increasing case-sensitive order

All parameters are optional, and if not used, their default values are as mentioned below. Boolean values default to ``false`` and the rest default to an empty list. Examples of each parameter's usage can be found below.


<capture_definition>
^^^^^^^^^^^^^^^^^^^^

* ``<capture_definition>`` can be in the following formats
    * ``<regex>`` - ``<regex>`` defines regex capturing groups, up to 10 captured strings based on ``<regex>`` will be added to the cache key.
    * ``/<regex>/<replacement>/`` - ``<regex>`` defines regex capturing groups, ``<replacement>`` defines a pattern where the captured strings referenced with ``$0`` ... ``$9`` will be substituted and the result will be added to the cache key.


Cache key elements separator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
* ``--separator=<string>`` - the cache key is constructed by extracting elements from HTTP URI and headers or by using the UA classifiers and they are appended during the key construction and separated by ``/`` (by default). This options allows to override the default separator to any string (including an empty string).


How to run the plugin
=====================

The plugin can run as a global plugin (a single global instance configured using `plugin.config`) or as per-remap plugin (a separate instance configured per remap rule in `remap.config`).

Global instance
^^^^^^^^^^^^^^^

::

  $ cat plugin.config
  cachekey.so \
      --include-params=a,b,c \
      --sort-params=true


Per-remap instance
^^^^^^^^^^^^^^^^^^

::

  $cat remap.config
  map http://www.example.com http://www.origin.com \
      @plugin=cachekey.so \
          @pparam=--include-params=a,b,c \
          @pparam=--sort-params=true


If both global and per-remap instance are used the per-remap configuration would take precedence (per-remap configuration would be applied and the global configuration ignored).

Because of the ATS core (remap) and the CacheKey plugin implementation there is a slight difference between the global and the per-remap functionality when ``--uri-type=remap`` is used.

* The global instance always uses the URI **after** remap (at ``TS_HTTP_POST_REMAP_HOOK``).

* The per-remap instance uses the URI **during** remap (after ``TS_HTTP_PRE_REMAP_HOOK`` and  before ``TS_HTTP_POST_REMAP_HOOK``) which leads to a different URI to be used depending on plugin order in the remap rule.

    * If CacheKey plugin is the first plugin in the remap rule the URI used will be practically the same as the pristine URI.
    * If the CacheKey plugin is the last plugin in the remap rule (which is right before ``TS_HTTP_POST_REMAP_HOOK``) the behavior will be simillar to the global instnance.


Detailed examples and troubleshooting
=====================================

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

The following would capture from the ``Authorization`` header and will add the captured element to the cache key ::

  @plugin=cachekey.so \
      @pparam=--capture-header=Authorization:/AWS\s(?<clientID>[^:]+).*/clientID:$1/

If the request looks like the following::

  http://example-cdn.com/path/file
  Authorization: AWS MKIARYMOG51PT0DLD:DLiWQ2lyS49H4Zyx34kW0URtg6s=

Cache key would be set to::

  /example-cdn.com/80/clientID:MKIARYMOG51PTCKQ0DLD/path/file


HTTP Cookies
^^^^^^^^^^^^

Include certain cookies in the cache key
""""""""""""""""""""""""""""""""""""""""

The following headers ``CookieA`` and ``CookieB`` will be used when constructing the cache key and the rest will be ignored. ::

  @plugin=cachekey.so @pparam=--include-headers=CookieA,CookieB


Prefix (host, port, capture and replace from URI)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Replacing host:port with a static cache key prefix
"""""""""""""""""""""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameter in the remap rule. ::

  @plugin=cachekey.so @pparam=--static-prefix=static_prefix

the cache key will be prefixed with ``/static_prefix`` instead of ``host:port`` when ``--static-prefix`` is not used.

Capturing from the host:port and adding it to the prefix section
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameter in the remap rule. ::

  @plugin=cachekey.so \
      @pparam=--capture-prefix=(test_prefix).*:([^\s\/$]*)

the cache key will be prefixed with ``/test_prefix/80`` instead of ``test_prefix_371.example.com:80`` when ``--capture-prefix`` is not used.

Capturing from the entire URI and adding it to the prefix section
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameter in the remap rule. ::

  @plugin=cachekey.so \
      @pparam=--capture-prefix-uri=/(test_prefix).*:.*(object).*$/$1_$2/

and if the request URI is the following ::

  http://test_prefix_123.example.com/path/to/object?a=1&b=2&c=3

the the cache key will be prefixed with ``/test_prefix_object`` instead of ``test_prefix_123.example.com:80`` when ``--capture-prefix-uri`` is not used.

Combining prefix plugin parameters, i.e. --static-prefix and --capture-prefix
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
If the plugin is used with the following plugin parameters in the remap rule. ::

  @plugin=cachekey.so \
      @pparam=--capture-prefix=(test_prefix).*:([^\s\/$]*) \
      @pparam=--static-prefix=static_prefix

the cache key will be prefixed with ``/static_prefix/test_prefix/80`` instead of ``test_prefix_371.example.com:80`` when either ``--capture-prefix`` nor ``--static-prefix`` are used.


Path, capture and replace from the path or entire URI
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Capture and replace groups from path for the "Path" section
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""

If the plugin is used with the following plugin parameter in the remap rule. ::

  @plugin=cachekey.so \
      @pparam=--capture-path=/.*(object).*/const_path_$1/

and the request URI is the following ::

  http://test_path_123.example.com/path/to/object?a=1&b=2&c=3

then the cache key will have ``/const_path_object`` in the path section of the cache key instead of ``/path/to/object`` when either ``--capture-path`` nor ``--capture-path-uri`` are used.


Capture and replace groups from whole URI for the "Path" section
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""

If the plugin is used with the following plugin parameter in the remap rule. ::

  @plugin=cachekey.so \
      @pparam=--capture-path-uri=/(test_path).*(object).*/$1_$2/

and the request URI is the following ::

  http://test_path_123.example.com/path/to/object?a=1&b=2&c=3

the the cache key will have ``/test_path_object`` in the path section of the cache key instead of ``/path/to/object`` when either ``--capture-path`` nor ``--capture-path-uri`` are used.


Combining path plugin parameters --capture-path and --capture-path-uri
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""

If the plugin is used with the following plugin parameters in the remap rule. ::

  @plugin=cachekey.so \
      @pparam=--capture-path=/.*(object).*/const_path_$1/ \
      @pparam=--capture-path-uri=/(test_path).*(object).*/$1_$2/

and the request URI is the following ::

  http://test_path_123.example.com/path/to/object?a=1&b=2&c=3

the the cache key will have ``/test_path_object/const_path_object`` in the path section of the cache key instead of ``/path/to/object`` when either ``--capture-path`` nor ``--capture-path-uri`` are used.

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


Cacheurl plugin to cachekey plugin migration
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The plugin `cachekey` was not meant to replace the cacheurl plugin in terms of having exactly the same cache key strings generated. It just allows the operator to exctract elements from the HTTP URI in the same way the `cacheurl` does (through a regular expression, please see `<capture_definition>` above).

The following examples demonstrate different ways to achieve `cacheurl` compatibility on a cache key string level in order to avoid invalidation of the cache. 

The operator could use `--capture-path-uri`, `--capture-path`, `--capture-prefix-uri`, `--capture-prefix` to capture elements from the URI, path and authority elements.

By using `--separator=<string>` the operator could override the default separator to an empty string `--separator=` and thus make sure there are no cache key element separators.


Example 1: Let us say we have a capture definition used in `cacheurl`. Now by using `--capture-prefix-uri` one could extract elements through the same caplture definition used with `cacheurl`, remove the cache key element separator `--separator=` and by using `--capture-path-uri` could remove the URI path and by using `--remove-all-params=true` could remove the query string::

  @plugin=cachekey.so \
      @pparam=--capture-prefix-uri=/.*/$0/ \
      @pparam=--capture-path-uri=/.*// \
      @pparam=--remove-all-params=true \
      @pparam=--separator=

Example 2: A more efficient way would be achieved by using `--capture-prefix-uri` to capture from the URI, remove the cache key element separator `--separator=`  and by using `--remove-path` to remove the URI path and `--remove-all-params=true` to remove the query string::

  @plugin=cachekey.so \
      @pparam=--capture-prefix-uri=/.*/$0/ \
      @pparam=--remove-path=true \
      @pparam=--remove-all-params=true \
      @pparam=--separator=

Example 3: Same result as the above but this time by using `--capture-path-uri` to capture from the URI, remove the cache key element separator `--separator=` and by using `--remove-prefix` to remove the URI authority elements and by using `--remove-all-params=true` to remove the query string::

    @plugin=cachekey.so \
        @pparam=--capture-path-uri=/(.*)/$0/ \
        @pparam=--remove-prefix=true \
        @pparam=--remove-all-params=true \
        @pparam=--separator=

Example 4: Let us say that we would like to capture from URI in similar to `cacheurl` way but also sort the query parameters (which is not supported by `cacheurl`). We could achieve that by using `--capture-prefix-uri` to capture by using a caplture definition to process the URI before `?`  and using `--remove-path` to remove the URI path and `--sort-params=true` to sort the query parameters::

    @plugin=cachekey.so \
        @pparam=--capture-prefix-uri=/([^?]*)/$1/ \
        @pparam=--remove-path=true \
        @pparam=--sort-params=true \
        @pparam=--separator=
