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

2. There are optional arguments that can be passed to the above ``esi.so`` entry:

- ``--private-response`` will add private cache control and expires headers to the processed ESI document.
- ``--packed-node-support`` will enable the support for using the packed node feature, which will improve the
  performance of parsing cached ESI document. As mentioned below, this option is not extensively tested and is therefore
  not recommended for production environments
- ``--disable-gzip-output`` will disable gzipped output for output which would **not** already be gzipeed anyway.
- ``--first-byte-flush`` will enable the first byte flush feature, which will flush content to users as soon as the entire
  ESI document is received and parsed without all ESI includes fetched. The flushing will stop at the ESI include markup
  till that include is fetched.
- ``--max-inclusion-depth <max-depth>`` controls the maximum depth of recursive ESI inclusion allowed (between 0 and 9).
  Default is 3.
- ``--include-host-allow <regex>`` restricts which hostnames may appear in ``<esi:include src=...>`` after variable
  expansion. The post-expansion hostname (everything between ``://`` and the next ``/``, ``:``, ``?``, or ``#``, with any
  ``user@`` prefix and IPv6 brackets stripped) must fully match the PCRE-syntax regex (case-insensitive). If unset,
  scheme/private-host checks still apply but any non-private host is permitted. One exception applies regardless of this
  setting (and regardless of ``--allow-private-include-hosts``): a host containing a ``%`` character is always rejected
  as ``private-host``. This blocks IPv6 zone IDs (e.g. ``[fe80::1%25eth0]``, which select a network interface and are
  only meaningful for link-local addresses) and percent-encoding tricks in the host. Rejected includes increment
  ``esi.n_include_errs`` and the offending URL is logged with any ``user:password@`` portion redacted to ``***@``.
  Because this is a security control, a regex that fails to compile causes plugin initialization to fail (the plugin
  refuses to load with no allowlist rather than silently fail open).

  .. important::

     The private-host denylist does **not** perform DNS resolution. It only classifies hosts that are IP literals
     (e.g. ``10.0.0.1``, ``[fe80::1]``) or localhost-style names (``localhost``, ``*.localhost``). An ordinary DNS
     hostname such as ``internal.example.com`` is treated as a non-private host and is permitted when no allowlist is
     set, *even if it resolves to a private or link-local address*. This means the built-in denylist alone does not
     protect against SSRF via DNS — including DNS rebinding, where a name resolves to a public address at validation
     time and a private one when the include is fetched. To constrain ``esi:include`` targets to hosts you trust, you
     must configure ``--include-host-allow`` with an explicit allowlist; do not rely on the private-host denylist for
     hostname-based SSRF protection.
- ``--allow-private-include-hosts`` disables the default denylist that rejects ``esi:include`` URLs whose host parses to
  a non-globally-routable or otherwise reserved IP address. The intent is to fail closed: anything that is not ordinary
  public address space is treated as private. For IPv4 this covers the unspecified/"this network" block
  (``0.0.0.0/8``), loopback (``127.0.0.0/8``), link-local (``169.254.0.0/16``, including the cloud metadata address
  ``169.254.169.254``), RFC 1918 (``10.0.0.0/8``, ``172.16.0.0/12``, ``192.168.0.0/16``), CGNAT (``100.64.0.0/10``),
  IETF protocol assignments (``192.0.0.0/24``), the TEST-NET ranges (``192.0.2.0/24``, ``198.51.100.0/24``,
  ``203.0.113.0/24``), benchmarking (``198.18.0.0/15``), multicast (``224.0.0.0/4``), reserved (``240.0.0.0/4``), and the
  broadcast address (``255.255.255.255``). For IPv6 it covers the unspecified address (``::``), loopback (``::1``),
  link-local (``fe80::/10``), unique-local (``fc00::/7``), and multicast (``ff00::/8``); IPv4-mapped (``::ffff:0:0/96``)
  and NAT64-encoded (``64:ff9b::/96``) addresses are unwrapped and re-checked against the IPv4 rules above, and any
  address carrying a zone id is treated as private. The hostname ``localhost`` (and ``*.localhost``) is also rejected.
  Non-canonical numeric IPv4 forms (decimal, octal, hex, or shortcut notations such as ``2130706433`` or ``0x7f000001``)
  are rejected outright. Enable this flag only if you intentionally use ESI to assemble responses from internal-IP
  backends. Schemes other than ``http`` and ``https`` are always rejected regardless of this flag.

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
