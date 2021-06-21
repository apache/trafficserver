.. _admin-plugins-esi:
.. include:: ../../common.defs

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

Supported ESI tags:

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

Extended ESI tags: ``esi:special-include``

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

Compilation and Installation
============================

This plugin is considered stable and is included with |TS| by default. There
are no special steps necessary for it to be built and installed.

Enabling ESI
============

1. First, enable the ESI plugin by adding an entry for it in :file:`plugin.config`. Here is an example of such an entry
   without passing any optional arguments to ESI:

::

    esi.so

2. There are four optional arguments that can be passed to the above ``esi.so`` entry:

- ``--private-response`` will add private cache control and expires headers to the processed ESI document.
- ``--packed-node-support`` will enable the support for using the packed node feature, which will improve the
  performance of parsing cached ESI document. As mentioned below, this option is not extensively tested and is therefore
  not recommended for production environments
- ``--disable-gzip-output`` will disable gzipped output for output which would **not** already be gzipeed anyway.
- ``--first-byte-flush`` will enable the first byte flush feature, which will flush content to users as soon as the entire
  ESI document is received and parsed without all ESI includes fetched. The flushing will stop at the ESI include markup
  till that include is fetched.

3. ``HTTP_COOKIE`` variable support is turned off by default. It can be turned on with ``-f <handler_config>`` or
   ``-handler <handler_config>``. For example:

::

    esi.so -f handler.conf

The ``handler.conf`` file then contains the list of allowed cookie names. For example, to allow the ``A`` and ``LOGIN``
cookies, the file will look like the following:

::

    allowlistCookie A
    allowlistCookie LOGIN

You can also allow all cookies for ``HTTP_COOKIE`` variable by using a wildcard character. For example:

::

    allowlistCookie *

4. An entry in :file:`remap.config` will be needed to map to the orginer server providing the ESI response. Assume that
   the ATS proxy is ``abc.com``, your origin server is ``xyz.com``, and the URI containing ESI markup is
   ``http://xyz.com/esi.php``. In this case, the following line in :file:`remap.config` will be needed:

::

    map http://abc.com/esi.php http://xyz.com/esi.php

5. Your response should contain ESI markup and a response header of ``X-Esi: 1``. Here is a PHP example:

::

    <?php   header('X-Esi: 1'); ?>
    <html>
    <body>
    Hello, <esi:include src="http://abc.com/date.php"/>
    </body>
    </html>

6. You will also need a mapping for the resource in the ESI include (``http://abc.com/date.php`` in this case) in
   :file:`remap.config` if it is not already present:

::

    map http://abc.com/date.php http://xyz.com/date.php

Or if both your ESI response and the ESI include comes from the same origin server, your :file:`remap.config` entry can
have the following single generic rule for all resources instead of separate rules for ``date.php`` and ``esi.php``:

::

    map http://abc.com/ http://xyz.com/

7. Here is sample PHP content for ``date.php``:

::

    <?php
    header ("Cache-control: no-cache");
    echo date('l jS \of F Y h:i:s A');
    ?>

Useful Notes
============

1. With proper cache control headers for each, the ESI response and the ESI include responses can be cached separately.
   This is extremely useful for rendering a page with multiple modules. The page layout can be an ESI response with
   multiple ESI includes, each for a different module. Thus |TS| can have a single cached entry for the page layout ESI response
   while each individual ESI included responses can also be cached separately, each with a different duration per their
   cache-control headers.

2. We do **not** recommend running the plugin with "packed node support" because it is not fully tested.

Differences from Spec - http://www.w3.org/TR/esi-lang
=====================================================

1. ``<esi:include>`` does not support "alt" and "onerror" attributes.

2. ``<esi:inline>`` is not supported.

3. You cannot have ``<esi:try>`` inside another ``<esi:try>``.

4. ``HTTP_USER_AGENT`` variable is not supported.

5. ``HTTP_COOKIE`` supports fetching for sub-key.

6. ``HTTP_HEADER`` supports accessing request headers as variables except "Cookie".
