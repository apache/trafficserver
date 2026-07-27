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

.. _admin-plugins-url-sig:

Signed URL Plugin
*****************

   .. note::

    The URL Sig protocol is old and unlikely to be standardized. Prefer :doc:`uri_signing.en`.

This plugin checks a signature query string on a URL and rejects (HTTP ``403``)
or redirects (HTTP ``302``) when the check fails. The signature is based on a
secret key that both a signing portal and the |TS| cache share. The algorithm
for the signature may be either ``MD5`` or ``SHA1``. When the signature check
passes, the query string of the request is stripped and continues to process as
if there were no query string at all.

Architecture
============

The plugin is split into cache-agnostic core logic and a thin ATS adapter:

``url_sig.h``
   Public header. Config structs, constants, function declarations. No ATS
   dependencies.

``url_sig_config.cc``
   Config file parser. Reads from ``std::istream``.

``url_sig_verify.cc``
   URL validation — parameter extraction, HMAC computation, signature
   comparison. Depends only on OpenSSL and standard C++20.

``url_sig.cc``
   ATS remap plugin glue. Implements ``TSRemap*`` hooks, delegates to core.

``test_url_sig.cc``
   Catch2 unit tests for core logic.

``genkeys.go``
   Go tool to generate a config file with random keys.

``sign.go``
   Go tool to sign URLs (produces a curl command).

Installation
============

To make this plugin available, you must enable experimental plugins when
building |TS| by passing ``-DBUILD_EXPERIMENTAL_PLUGINS=ON`` to the ``cmake``
command when building. Alternatively, enable just this plugin with
``-DENABLE_URL_SIG=ON``.

To build the unit tests, also pass ``-DBUILD_TESTING=ON``::

   cmake --preset dev -DENABLE_URL_SIG=ON -DBUILD_TESTING=ON
   cmake --build build-dev --target url_sig
   cmake --build build-dev --target test_url_sig
   ./build-dev/plugins/experimental/url_sig/test_url_sig

Configuration
=============

Configuring URL signature verification within |TS| using this plugin is a two
step process. First, you must generate a configuration file containing the list
of valid signing keys. Secondly, you must indicate to |TS| which URLs require
valid signatures.

Config File Format
------------------

The config file is a simple ``key = value`` text file. Lines starting with
``#`` are comments. The file must contain at least one key and an ``error_url``
line.

=================  =================================  =============================================
Key                Value                              Description
=================  =================================  =============================================
``key0``–``key15`` string (max 255 chars)             Shared HMAC signing keys.
``error_url``      ``403`` or ``302 <redirect_url>``  Response for failed validation.
``sig_anchor``     string                             Anchor name for path-parameter mode.
``excl_regex``     PCRE regex pattern                 URLs matching skip signature validation.
``url_type``       ``pristine`` or ``remap``          Which URL to validate against (default: remap).
``ignore_expiry``  ``true``                           Disable expiration checking (debug only).
=================  =================================  =============================================

Example configuration::

   # Shared signing keys (up to 16, index 0–15).
   key0 = YwG7iAxDo6Gaa38KJOceV4nsxiAJZ3DS
   key1 = nLE3SZKRgaNM9hLz_HnIvrCw_GtTUJT1
   key2 = YicZbmr6KlxfxPTJ3p9vYhARdPQ9WJYZ
   key3 = DTV4Tcn046eM9BzJMeYrYpm3kbqOtBs7
   key4 = C1r6R6MINoQd5YSH25fU66tuRhhz3fs_
   key5 = l4dxe6YEpYbJtyiOmX5mafhwKImC5kej
   key6 = ekKNHXu9_oOC5eqIGJVxV0vI9FYvKVya
   key7 = BrjibTmpTTuhMHqphkQAuCWA0Zg97WQB
   key8 = rEtWLb1jcYoq9VG8Z8TKgX4GxBuro20J
   key9 = mrP_6ibDBG4iYpfDB6W8yn3ZyGmdwc6M
   key10 = tbzoTTGZXPLcvpswCQCYz1DAIZcAOGyX
   key11 = lWsn6gUeSEW79Fk2kwKVfzhVG87EXLna
   key12 = Riox0SmGtBWsrieLUHVWtpj18STM4MP1
   key13 = kBsn332B7yG3HdcV7Tw51pkvHod7_84l
   key14 = hYI4GUoIlZRf0AyuXkT3QLvBMEoFxkma
   key15 = EIgJKwIR0LU9CUeTUdVtjMgGmxeCIbdg
   error_url = 403

Additional options example::

   sig_anchor = urlsig
   excl_regex = (/crossdomain.xml|/clientaccesspolicy.xml|/test.html)
   url_type = pristine
   ignore_expiry = true

.. important::

   The configuration file contains the full set of secret keys which |TS| will
   be using to verify incoming requests, and as such should be treated with as
   much care as any other file in your infrastructure containing keys, pass
   phrases, and other sensitive data.

Generating Keys
---------------

The plugin ships with a Go tool to generate random keys. Run it with
``go run``::

   go run genkeys.go > /etc/trafficserver/url_sig.config

No Go modules or dependencies are needed — the file uses only the Go standard
library.

The original Perl script ``genkeys.pl`` is still available for backward
compatibility but requires the ``Digest::SHA`` and ``MIME::Base64::URLSafe``
modules.

Requiring Signatures on URLs
-----------------------------

Modify your :file:`remap.config` to include this plugin for any rules you
wish to protect.

Two parameters for each remap rule are required, and a third one is optional::

   @plugin=url_sig.so @pparam=<config file> @pparam=pristineurl

The first enables the plugin for the rule. The second specifies the location of
the configuration file (relative to ``etc/trafficserver/`` unless an absolute
path). The optional third parameter causes authentication to be performed on
the original (pristine) URL as received from the client.

Example::

   map http://cdn.example.com http://origin.example.com \
       @plugin=url_sig.so @pparam=url_sig.config

With pristine URL mode::

   map http://cdn.example.com http://origin.example.com \
       @plugin=url_sig.so @pparam=url_sig.config @pparam=pristineurl

.. note::

   To be consistent, the config file option ``url_type = pristine`` should
   be preferred over using a plugin argument.

Signing a URL
=============

Signing is performed by adding several query parameters to a URL before
redirecting the client. The parameters are all generated by your signing portal
application. A hash is then calculated over the constructed URL and attached as
the final query parameter.

.. note::

   Ordering matters. The signature is a hash of the entire URL up to and
   including the ``S=`` substring. The ``S=`` value itself is not included in
   the hash input.

Signing Parameters
------------------

=====  ==========  ===========  =================================================================
Param  Name        Required     Description
=====  ==========  ===========  =================================================================
``C``  Client IP   optional     Locks signature to a specific client IP (IPv4 or IPv6).
``E``  Expiration  required     Seconds since Unix epoch when the signature expires.
``A``  Algorithm   required     ``1`` = HMAC-SHA1, ``2`` = HMAC-MD5.
``K``  Key Index   required     Index (0–15) of the key in the config file.
``P``  Parts       required     Bitmask of URL parts to include in the signature (see below).
``S``  Signature   required     Hex-encoded HMAC digest. **Must be last parameter.**
=====  ==========  ===========  =================================================================

Parts Mask
~~~~~~~~~~

The URL (minus scheme) is split by ``/``. Each character in the parts string
controls whether that segment is included in the signed string:

==========  ================================================================
Value       Effect
==========  ================================================================
``1``       Use the FQDN and all directory parts for signature verification.
``01``      Ignore the FQDN, but verify using all directory parts.
``0110``    Ignore the FQDN, use only the first two directory parts.
``110``     Use the FQDN and first directory, ignore the remainder.
==========  ================================================================

If the parts string is shorter than the number of URL segments, the last
character repeats for remaining segments.

Query String Mode (Default)
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Parameters are appended as a standard query string::

   http://cdn.example.com/path/file.ts?E=1700000000&A=1&K=3&P=1&S=9e2828d5...

Path Parameter Mode
~~~~~~~~~~~~~~~~~~~

Parameters may instead be base64-encoded and embedded in the URL path before
the filename. This is useful when origin query parameters must be preserved
independently of the signing parameters.

Configure ``sig_anchor`` in the config file and use ``--pathparams
--siganchor`` when signing::

   http://cdn.example.com/vod/t;urlsig=O0U9MTQ2.../prog_index.m3u8?appid=2&t=1

Application query parameters follow the filename and are never part of the
signed string.

Using the Go Signing Tool
-------------------------

The Go signing tool ``sign.go`` produces a curl command for testing. It
requires only the Go standard library.

**Basic query string signing:**

.. code-block:: bash

   go run sign.go \
       --url http://cdn.example.com/video/segment.ts \
       --useparts 1 \
       --algorithm 1 \
       --duration 3600 \
       --keyindex 3 \
       --key DTV4Tcn046eM9BzJMeYrYpm3kbqOtBs7

**With client IP restriction:**

.. code-block:: bash

   go run sign.go \
       --url http://cdn.example.com/video/segment.ts \
       --useparts 1 --algorithm 1 --duration 3600 \
       --keyindex 3 --key DTV4Tcn046eM9BzJMeYrYpm3kbqOtBs7 \
       --client 10.10.10.10

**Path parameter mode with sig anchor:**

.. code-block:: bash

   go run sign.go \
       --url "http://cdn.example.com/vod/t/prog_index.m3u8?appid=2&t=1" \
       --useparts 1 --algorithm 1 --duration 86400 \
       --keyindex 3 --key kSCE1_uBREdGI3TPnr_dXKc9f_J4ZV2f \
       --pathparams --siganchor urlsig

**Through a proxy:**

.. code-block:: bash

   go run sign.go \
       --url http://cdn.example.com/ \
       --useparts 1 --algorithm 1 --duration 60 \
       --keyindex 0 --key mykey \
       --proxy http://localhost:8080

**Verbose mode** (shows signed string and digest on stderr):

.. code-block:: bash

   go run sign.go --verbose \
       --url http://cdn.example.com/ \
       --useparts 1 --algorithm 1 --duration 60 \
       --keyindex 0 --key mykey

sign.go Flags
~~~~~~~~~~~~~

================  ========  =========  =========================================
Flag              Required  Default    Description
================  ========  =========  =========================================
``--url``         yes                  Full URL to sign.
``--useparts``    yes                  Parts bitmask string.
``--duration``    yes                  Signature lifetime in seconds.
``--key``         yes                  Signing key string.
``--keyindex``    yes       ``0``      Key index (0–15).
``--algorithm``   no        ``1``      ``1`` = HMAC-SHA1, ``2`` = HMAC-MD5.
``--client``      no                   Lock signature to client IP.
``--pathparams``  no        ``false``  Use path parameter mode.
``--siganchor``   no                   Anchor name for path params.
``--proxy``       no                   Proxy URL:port for curl output.
``--verbose``     no        ``false``  Print signing details to stderr.
================  ========  =========  =========================================

The original Perl script ``sign.pl`` is still available with equivalent
functionality. It requires ``Digest::SHA``, ``Digest::HMAC_MD5``, and
``MIME::Base64::URLSafe``.

Debugging
=========

To include debugging output for this plugin in your |TS| logs, adjust the
values for :ts:cv:`proxy.config.diags.debug.enabled` and
:ts:cv:`proxy.config.diags.debug.tags` in your :file:`records.yaml`:

.. code-block:: yaml

   records:
     diags:
       debug:
         enabled: 1
         tags: url_sig

Then reload:

.. code-block:: bash

   traffic_ctl config reload

- Debug output goes to ``traffic.out`` / ``diags.log``.
- Failed signature checks are logged to ``error.log``.

Walkthrough Example
===================

#. **Generate keys** (replacing the output location with something appropriate
   to your |TS| installation)::

      go run genkeys.go > /etc/trafficserver/url_sig.config

#. **Verify the config** looks correct::

      cat /etc/trafficserver/url_sig.config

   You should see 16 key lines and an ``error_url`` line.

#. **Configure a remap rule** in :file:`remap.config`::

      map http://cdn.example.com http://origin.example.com \
          @plugin=url_sig.so @pparam=url_sig.config

#. **Reload** |TS| configuration::

      traffic_ctl config reload

#. **Test an unsigned request** (should get 403)::

      curl -vs http://localhost:8080/ -H 'Host: cdn.example.com'

   Expected response: ``HTTP/1.1 403 Forbidden`` with body
   ``Authorization Denied``.

#. **Sign a URL** using key3 from your config file::

      go run sign.go \
          --url http://cdn.example.com/ \
          --useparts 1 --algorithm 1 --duration 60 \
          --keyindex 3 --key <key3_value_from_config>

#. **Test the signed URL**. Copy the curl command from sign.go output. If
   hitting localhost, add ``-H 'Host: cdn.example.com'``::

      curl -s -o /dev/null -v --max-redirs 0 \
          -H 'Host: cdn.example.com' \
          'http://localhost:8080/?E=1700000060&A=1&K=3&P=1&S=<signature>'

   Expected response: ``HTTP/1.1 200 OK``.

#. **Test path parameter mode**. Add ``sig_anchor = urlsig`` to
   ``url_sig.config``, reload, then::

      go run sign.go \
          --url "http://cdn.example.com/vod/t/file.ts?appid=2" \
          --useparts 1 --algorithm 1 --duration 86400 \
          --keyindex 3 --key <key3_value> \
          --pathparams --siganchor urlsig
