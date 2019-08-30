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

.. _admin-plugins-regex-remap:

Regex Remap Plugin
******************

This allows you to configure mapping rules based on regular expressions.
This is similar to what you can accomplish using mod_rewrite in Apache
httpd, but obviously not as flexible or sophisticated (yet).

To use this plugin, configure a remap.config rule like ::

    map http://a.com http://b.com @plugin=regex_remap.so @pparam=maps.reg

The file name parameter is always required. Unless an absolute path
is specified, the file name is assumed to be a path relative to the
Traffic Server configuration directory.

The regular expressions listed in the configuration file are evaluated
sequentially. When a regular expression is positively matched against
a request URL, evaluation is stopped and the rewrite rule is applied.
If none of the regular expressions are a match, the default destination
URL is applied (``http://b.com`` in the example above).

An optional argument (``@pparam``) with the string "``profile``\ " will
enable profiling of this regex remap rule, e.g. ::

    ... @pparam=maps.reg @pparam=profile

Profiling is very low overhead, and the information is dumped to
``traffic.out``, located in the log directory. This information is
useful to optimize the order of your regular expression, such that the
most common matches appears early in the file. In order to force a
profile dump, you can do ::

    $ sudo touch remap.config
    $ sudo traffic_ctl config reload

By default, only the path and query string of the URL are provided for
the regular expressions to match. The following optional parameters can
be used to modify the plugin instance behavior ::

    @pparam=[no-]method              [default: off]
    @pparam=[no-]query-string        [default: on]
    @pparam=[no-]matrix-parameters   [default: off]
    @pparam=[no-]host                [default: off]

If you wish to match on the HTTP method used (e.g. "``GET``\ "),
you must use the option ``@pparam=method``. e.g. ::

    ... @pparam=maps.reg @pparam=method

With this enabled, the string that you will need to match will look
like ::

    GET/path?query=bar

The methods are always all upper-case, and always followed by one single
space. There is no space between the method and the rest of the URL (or
URI path).

By default, the query string is part of the string that is matched
again, to turn this off use the option 'no-query-string', e.g. ::

    ... @pparam=maps.reg @pparam=no-query-string

You can also include the matrix parameters in the string, using
the option 'matrix-parameters', e.g. ::

    ... @pparam=maps.reg @pparam=matrix-parameters

Finally, to match on the host as well, use the option 'host', e.g. ::

    ... @pparam=maps.reg @pparam=host

With this enabled, the string that you will need to match will look like ::

    //host/path?query=bar


A typical regex would look like ::

    ^/(ogre.*)/more     http://www.ogre.com/$h/$0/$1

The regular expression must not contain any white spaces!

When the regular expression is matched, only the URL path + query string
is matched (without any of the optional configuration options). The path
will always start with a "/". Various substitution strings are allowed
on the right hand side during evaluation ::

    $0     - The entire matched string
    $1-9   - Regular expression groups ($1 first group etc.)
    $h     - The host as used in the "to" portion of the remap rule. For a long time it was the original host header from the request.
    $f     - The host as used in the "from" portion of the remap rule
    $t     - The host as used in the "to" portion of the remap rule
    $p     - The original port number
    $s     - The scheme (e.g. http) of the request
    $P     - The entire path of the request
    $q     - The query part of the request
    $r     - The path parameters of the request (not implemented yet)
    $c     - The cookie string from the request
    $i     - The client IP for this request

.. note::

    The ``$0`` substitution expands to the characters that were
    matched by the regular expression, not to the entire string that
    the regular expression was matched against.

You can also provide options, similar to how you configure your
remap.config. The following options are available ::

    @status=<nnn>               - Force the response code to <nnn>
    @active_timeout=<nnn>       - Active timeout (in ms)
    @no_activity_timeout=<nnn>  - No activity timeout (in ms)
    @connect_timeout=<nnn>      - Connect timeouts (in ms)
    @dns_timeout=<nnn>          - Connect timeouts (in ms)

    @overridable-config=<value> - see :ref:`ts-overridable-config`

    @caseless                   - Make regular expressions case insensitive
    @lowercase_substitutions    - Turn on (enable) lower case substitutions


This can be useful to force a particular response for some URLs, e.g. ::

    ^/(ogre.*)/bad      http://www.examle.com/  @status=404

Or, to force a 302 redirect ::

    ^/oldurl/(.*)$      http://news.example.com/new/$1 @status=302

Setting the status to 301 or 302 will force the new URL to be used
as a redirect (Location:).
