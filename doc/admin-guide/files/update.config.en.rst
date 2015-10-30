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

=============
update.config
=============

.. configfile:: update.config

The :file:`update.config` file controls how Traffic Server performs a
scheduled update of specific local cache content. The file contains a
list of URLs specifying objects that you want to schedule for update.

A scheduled update performs a local HTTP ``GET`` on the objects at the
specific time or interval. You can control the following parameters for
each specified object:

-  The URL
-  URL-specific request headers, which overrides the default
-  The update time and interval
-  The recursion depth

After you modify the :file:`update.config` file,
run the :option:`traffic_line -x`
command to apply changes. When you apply changes to one node in a
cluster, Traffic Server automatically applies the changes to all other
nodes in the cluster.

Supported Tag/Attribute Pairs
=============================

Scheduled update supports the following tag/attribute pairs when
performing recursive URL updates:

-  ``<a href=" ">``
-  ``<img src=" ">``
-  ``<img href=" ">``
-  ``<body background=" ">``
-  ``<frame src=" ">``
-  ``<iframe src=" ">``
-  ``<fig src=" ">``
-  ``<overlay src=" ">``
-  ``<applet code=" ">``
-  ``<script src=" ">``
-  ``<embed src=" ">``
-  ``<bgsound src=" ">``
-  ``<area href=" ">``
-  ``<base href=" ">``
-  ``<meta content=" ">``

Scheduled update is designed to operate on URL sets consisting of
hundreds of input URLs (expanded to thousands when recursive URLs are
included); it is *not* intended to operate on extremely large URL sets,
such as those used by Internet crawlers.

Format
======

Each line in the :file:`update.config` file uses the following format::

    URL\request_headers\offset_hour\interval\recursion_depth\

The following list describes each field.

.. _update-config-format-url:

*URL*
    HTTP-based URLs.

.. _update-config-format-request-headers:

*request_headers*
    Optional. A list of headers, separated by semicolons, passed in each
    ``GET`` request. You can define any request header that conforms to
    the HTTP specification; the default is no request header.

.. _update-config-format-offset-hour:

*offset_hour*
    The base hour used to derive the update periods. The range is 00-23
    hours.

.. _update-config-format-interval:

*interval*
    The interval (in seconds) at which updates should occur, starting at
    the offset hour.

.. _update-config-format-reecursion-depth:

*recursion_depth*
    The depth to which referenced URLs are recursively updated, starting
    at the given URL. This field applies only to HTTP.

Examples
========

An example HTTP scheduled update is provided below:

::

    http://www.company.com\User-Agent: noname user agent\13\3600\5\

The example specifies the URL and request headers, an offset hour of 13
(1 pm), an interval of one hour, and a recursion depth of 5. This would
result in updates at 13:00, 14:00, 15:00, and so on. To schedule an
update that occurs only once a day, use an interval value 86400 (i.e.,
24 hours x 60 minutes x 60 seconds = 86400).

.. XXX: The following seems misplaced here, and is probably better off placed in an apendix.

Specifying URL Regular Expressions (``url_regex``)
==================================================

This section describes how to specify a ``url_regex``. Entries of type
``url_regex`` within the configuration files use regular expressions to
perform a match.

The following list provides examples to show how to create a valid
``url_regex``.

``x``
    Matches the character ``x``

``.``
    Match any character

``^``
    Specifies beginning of line

``$``
    Specifies end of line

``[xyz]``
    A **character class**. In this case, the pattern matches either
    ``x``, ``y``, or\ ``z``

``[abj-oZ]``
    A **character class** with a range. This pattern matches ``a``,
    ``b``, any letter from ``j`` through ``o``, or ``Z``

``[^A-Z]``
    A **negated character class**. For example, this pattern matches any
    character except those in the class.

``r*``
    Zero or more ``r``, where ``r`` is any regular expression.

``r+``
    One or more ``r``, where ``r`` is any regular expression.

``r?``
    Zero or one ``r``, where ``r`` is any regular expression.

``r{2,5}``
    From two to five ``r``, where ``r`` is any regular expression.

``r{2,}``
    Two or more ``r``, where ``r`` is any regular expression.

``r{4}``
    Exactly four ``r``, where ``r`` is any regular expression.

``"[xyz]\"images"``
    The literal string ``[xyz]"images"``

``\X``
    If ``X`` is ``a, b, f, n, r, t,`` or ``v``, then the ``ANSI-C``
    interpretation of ``\x``; otherwise, a literal ``X``. This is used
    to escape operators such as ``*``

``\0``
    A ``NULL`` character

``\123``
    The character with octal value ``123``

``\x2a``
    The character with hexadecimal value ``2a``

``(r)``
    Matches an ``r``, where ``r`` is any regular expression. You can use
    parentheses to override precedence.

``rs``
    The regular expression ``r``, followed by the regular expression
    ``s``

``r|s``
    Either an ``r`` or an ``s``

``#<n>#``
    Inserts an **end node**, which causes regular expression matching to
    stop when reached. The value ``n`` is returned.

You can specify ``dest_domain=mydomain.com`` to match any host in
``mydomain.com``. Likewise, you can specify ``dest_domain=.`` to match
any request.

