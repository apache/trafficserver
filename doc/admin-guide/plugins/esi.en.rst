.. _admin-plugins-esi:

ESI Plugin
**********

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


This plugin implements the ESI specification.

Specification
=============

Supportted ESI tags:

::

    esi:include
    esi:remove
    esi:comment
    esi:vars
    esi:choose
    esi:when
    esi:otherwise
    esi:try
    esi:attempt
    esi:except
    <!--esi ... -->

extended ESI tags: *esi:special-include*

Supported variables:

::

    $(HTTP_HOST)
    $(HTTP_REFERER)
    $(HTTP_ACCEPT_LANGUAGE{name})
    $(HTTP_COOKIE{name}) or $(HTTP_COOKIE{name;subkey})
    $(QUERY_STRING{name})
    $(HTTP_HEADER{hdr_name})

Note: the name is the key name such as "username", "id" etc. For cookie support sub-name or sub-key, the format is:
name;subkey, such as "l;u", "l;t" etc. e.g. such cookie string: l=u=test&t=1350952328, the value of
$(HTTP_COOKIE{"l;u"}) is test and the value of $(HTTP_COOKIE{"l;t"}) is 1350952328

Compile and Installation
========================

This plugin is only built if the configure option ::

    --enable-experimental-plugins

is given at build time. Note that this plugin is built and installed in combination with the combo handler module, since
they share common code.

Enabling ESI
============

1. First we need to set up /usr/local/etc/trafficserver/plugin.config and make sure the following line is present.

::

    esi.so

2. There are four options you can add to the above.

- ``--private-response`` will add private cache control and expires header to the processed ESI document.
- ``--packed-node-support`` will enable the support for using packed node, which will improve the performance of parsing
  cached ESI document.
- ``--disable-gzip-output`` will disable gzipped output, which will NOT gzip the output anyway.
- ``--first-byte-flush`` will enable the first byte flush feature, which will flush content to users as soon as the entire
  ESI document is received and parsed without all ESI includes fetched (the flushing will stop at the ESI include markup
  till that include is fetched).

3. HTTP_COOKIE variable supported is turned off by default. You can turn it on with '-f' or '-handler option'

::

    esi.so -f handler.conf

And inside handler.conf you can provide the list of cookie name that is allowed.

::

    whitelistCookie A
    whitelistCookie LOGIN

We can also allow all cookie for HTTP_COOKIE variable by using a wildcard character. e.g.

::

    whitelistCookie *

4. We need a mapping for origin server response that contains the ESI markup. Assume that the ATS server is abc.com. And your origin server is xyz.com and the response containing ESI markup is http://xyz.com/esi.php. We will need
   the following line in /usr/local/etc/trafficserver/remap.config

::

    map http://abc.com/esi.php http://xyz.com/esi.php

5. Your response should contain ESI markup and a response header of 'X-Esi: 1'. e.g. using PHP,

::

    <?php   header('X-Esi: 1'); ?>
    <html>
    <body>
    Hello, <esi:include src="http://abc.com/date.php"/>
    </body>
    </html>

6. You will need a mapping for the src of the ESI include in remap.config if it is not already present.

::

    map http://abc.com/date.php http://xyz.com/date.php

Or if both your ESI response and the ESI include comes from the same origin server, you can have the following line in
remap.config instead to replace separate map rules for date.php and esi.php

::

    map http://abc.com/ http://xyz.com/

7. Here is a sample PHP for date.php

::

    <?php
    header ("Cache-control: no-cache");
    echo date('l jS \of F Y h:i:s A');
    ?>

Useful Note
===========

1. You can provide proper cache control header and the ESI response and ESI include response can be cached separately.
   It is extremely useful for rendering page with multiple modules. The page layout can be a ESI response with multiple
   ESI include include, each for different module. The page layour ESI response can be cached and each individual ESI
   include can also be cached with different duration.

2. You should run the plugin without using "packed node support" because it is not fully tested.

Differences from Spec - http://www.w3.org/TR/esi-lang
=====================================================

1. <esi:include> does not support "alt" and "onerror" attributes

2. <esi:inline> is not supported

3. You cannot have <esi:try> inside another <esi:try>

4. HTTP_USER_AGENT variable is not supported

5. HTTP_COOKIE supports fetching for sub-key

6. HTTP_HEADER supports accessing request headers as variables except "Cookie"
