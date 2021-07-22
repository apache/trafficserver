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

Installation
============

To make this plugin available, you must either enable experimental plugins
when building |TS|::

    ./configure --enable-experimental-plugins

Or use :program:`tsxs` to compile the plugin against your current |TS| build.
To do this, you must ensure that:

#. Development packages for |TS| are installed.

#. The :program:`tsxs` binary is in your path.

#. The version of this plugin you are building, and the version of |TS| against
   which you are building it are compatible.

Once those conditions are satisfied, enter the source directory for the plugin
and perform the following::

    make -f Makefile.tsxs
    make -f Makefile.tsxs install

Configuration
=============

Configuring URL signature verification within |TS| using this plugin is a two
step process. First, you must generate a configuration file containing the list
of valid signing keys. Secondly, you must indicate to |TS| which URLs require
valid signatures.

Generating Keys
---------------

This plugin comes with two Perl scripts which assist in generating signatures.
For |TS| to verify URL signatures, it must have the relevant keys. Using the
provided *genkeys* script, you can generate a suitable configuration file::

    ./genkeys.pl > url_sig.config

The resulting file will look something like the following, with the actual keys
differing (as they are generated randomly each time the script is run)::

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

This file should be placed in your |TS| ``etc`` directory, with permissions and
ownership set such that only the |TS| processes may read it.

.. important::

   The configuration file contains the full set of secret keys which |TS| will
   be using to verify incoming requests, and as such should be treated with as
   much care as any other file in your infrastructure containing keys, pass
   phrases, and other sensitive data. Unauthorized access to the contents of
   this file will allow others to spoof requests from your signing portal, thus
   defeating the entire purpose of using a signing portal in the first place.

Requiring Signatures on URLs
----------------------------

To require a valid signature, verified by a key from the list you generated
earlier, modify your :file:`remap.config` configuration to include this plugin
for any rules you wish it to affect.

Two parameters for each remap rule are required, and a third one is optional::

    @plugin=url_sig.so @pparam=<config file> @pparam=pristineurl

The first simply enables this plugin for the rule. The second specifies the
location of the configuration file containing your signing keys.  The third one,
if present, causes authentication to be performed on the original (pristine) URL
as received from the client. (The value of the parameter is not case sensitive.)

For example, if we wanted to restrict all paths under a ``/download`` directory
on our website ``foo.com`` we might have a remap line like this::

    map http://foo.com/download/ http://origin.server.tld/download/ \
        @plugin=url_sig.so @pparam=url_sig.config

.. note::

   To be consistent, the config file option `pristine = true` should
   be preferred over using a plugin argument.


Signing a URL
=============

Signing a URL is solely the responsibility of your signing portal service. This
requires that whatever application runs that service must also have a list of
your signing keys (generated earlier in Configuration_ and stored in the
``url_sig.config`` file in your |TS| configuration directory). How your signing
portal application is informed about, or stores, these keys is up to you, but
it is critical that the ``keyN`` index numbers are matched to the same keys.

Signing is performed by adding several query parameters to a URL, before
redirecting the client. The parameters' values are all generated by your
signing portal application, and then a hash is calculated by your portal, using
the entire URL just constructed, and attached as the final query parameter.

.. note::

   Ordering is important when adding the query parameters and generating the
   signature. The signature itself is a hash, using your chosen algorithm, of
   the entire URL to which you are about to redirect the client, up to and
   including the ``S=`` substring indicating the signature parameter.

The following list details all the query parameters you must add to the URL you
will hand back to the client for redirection.

Client IP
    The IP address of the client being redirected. This must be their IP as it
    will appear to your |TS| cache.  Both IP v4 and v6 addresses are supported::

        C=<ip address>

Expiration
    The time at which this signature will no longer be valid, expressed in
    seconds since epoch (and thus in UTC)::

        E=<seconds since epoch expiration>

Algorithm
    The hash algorithm which your signing portal application has elected to use
    for this signature::

        A=<algorithm number>

    The only supported values at this time are:

    ===== =========
    Value Algorithm
    ===== =========
    ``1`` HMAC_SHA1
    ``2`` HMAC_MD5
    ===== =========

Key Index
    The key number, from your plugin configuration, which was used for this
    signature. See Configuration_ for generating these keys and determining the
    index number of each::

        K=<key number>

Parts
    Configures which components of the URL to use for signature verification.
    The value of this parameter is a string of ones and zeros, each enabling
    or disabling the use of a URL part for signatures. The URL scheme (e.g.
    ``http://``) is never part of the signature. The first number of this
    parameter's value indicates whether to include the FQDN, and all remaining
    numbers determine whether to use the directory parts of the URL. If there
    are more directories in the URL than there are numbers in this parameter,
    the last number is repeated as many times as necessary::

        P=<parts specification>

    Examples include:

    ========== ================================================================
    Value      Effect
    ========== ================================================================
    ``1``      Use the FQDN and all directory parts for signature verification.
    ``01``     Ignore the FQDN, but verify using all directory parts.
    ``0110``   Ignore the FQDN, and use only the first two directory parts,
               skipping the remainder, for signatures.
    ``110``    Use the FQDN and first directory for signature verification, but
               ignore the remainder of the path.
    ========== ================================================================

Signature
    The actual signature hash::

        S=<signature hash>

    The hash should be calculated in accordance with the parts specification
    you have declared in the ``P=`` query parameter, which if you have chosen
    any value other than ``1`` may require additional URL parsing be performed
    by your signing portal.

    Additionally, all query parameters up to and including the ``S=`` substring
    for this parameter must be included, and must be in the same order as they
    are returned to the client for redirection. For obvious reasons, the value
    of this parameter is not included in the source string being hashed.

    As a simple example, if we are about to redirect a client to the URL
    ``https://foo.com/downloads/expensive-app.exe`` with signature verification
    enabled, then we will compute a signature on the following string::

        foo.com/downloads/expensive-app.exe?C=1.2.3.4&E=1453846938&A=1&K=2&P=1&S=

    And, assuming that *key2* from our secrets file matches our example in
    Configuration_, then our signature becomes::

        8c5cfa440458233452ee9b5b570063a0e71827f2

    Which is then appended to the URL for redirection as the value of the ``S``
    parameter.

    For an example implementation of signing which may be adapted for your own
    portal, refer to the file ``sign.pl`` included with the source code of this
    plugin.

Signature query parameters embedded in the URL path.

    Optionally signature query parameters may be embedded in an opaque base64 encoded container
    embedded in the URL path.  The format is  a semicolon, siganchor string, base64 encoded
    string.  ``url_sig`` automatically detects the use of embedded path parameters. The
    following example shows how to generate an embedded path parameters with ``sign.pl``::

      ./sign.pl --url "http://test-remap.domain.com/vod/t/prog_index.m3u8?appid=2&t=1" --useparts 1 \
      --algorithm 1 --duration 86400 --key kSCE1_uBREdGI3TPnr_dXKc9f_J4ZV2f --pathparams --siganchor urlsig

      curl -s -o /dev/null -v --max-redirs 0 'http://test-remap.domain.com/vod/t;urlsig=O0U9MTQ2MzkyOTM4NTtBPTE7Sz0zO1A9MTtTPTIxYzk2YWRiZWZk'

Other Config File Options
=========================

In addition to the keys and error_url, the following options are supported
in the configuration file::

    sig_anchor
        signed anchor string token in url
        default: no anchor

    excl_regex
        pcre regex for urls that aren't signed.
        default: no regex

    url_type
        which url to match against
        pristine or remap
        default: remap

     ignore_expiry
        option which assists in testing where the timestamp check is skipped
        DO NOT run with this set in production!
        default: false

Example::

    sig_anchor = urlsig
    excl_regex = (/crossdomain.xml|/clientaccesspolicy.xml|/test.html)
    url_type = pristine
    ignore_expiry = true


Edge Cache Debugging
====================

To include debugging output for this plugin in your |TS| logs, adjust the values
for :ts:cv:`proxy.config.diags.debug.enabled` and
:ts:cv:`proxy.config.diags.debug.tags` in your :file:`records.config` as so::

    CONFIG proxy.config.diags.debug.enabled INT 1
    CONFIG proxy.config.diags.debug.tags STRING url_sig

Once updated, issue a :option:`traffic_ctl config reload` to make the settings
active.

Example
=======

#. Enable experimental plugins when building |TS|::

    ./configure --enable-experimental-plugins

#. Generate a secrets configuration for |TS| (replacing the output location
   with something appropriate to your |TS| installation)::

    genkeys.pl  > /usr/local/trafficserver/etc/trafficserver/url_sig.config

#. Verify that your configuration looks like the following, with actual key
   values altered::

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

#. Enable signature verification for a remap rule in |TS| by modifying
   :file:`remap.config` (here we will just remap to Google's homepage for
   demonstrative purposes)::

    map http://test-remap.domain.com/download/foo http://google.com \
        @plugin=url_sig.so @pparam=url_sig.config

#. Reload your |TS| configuration to ensure the changes are active::

    traffic_ctl config reload

#. Attempt to access the now-protected URL without a valid signature. This will
   fail, and that is a good thing, as it demonstrates that |TS| now rejects any
   requests to paths matching that rule which do not include a valid signature.::

    $ curl -vs -H'Host: test-remap.domain.com' http://localhost:8080/download/foo
    * Adding handle: conn: 0x200f8a0
    * Adding handle: send: 0
    * Adding handle: recv: 0
    * Curl_addHandleToPipeline: length: 1
    * - Conn 0 (0x200f8a0) send_pipe: 1, recv_pipe: 0
    * About to connect() to localhost port 8080 (#0)
    *   Trying 127.0.0.1...
    * Connected to localhost (127.0.0.1) port 8080 (#0)
    > GET /download/foo HTTP/1.1
    > User-Agent: curl/7.32.0
    > Accept: */*
    > Host: test-remap.domain.com
    >
    < HTTP/1.1 403 Forbidden
    < Date: Tue, 15 Apr 2014 22:57:32 GMT
    < Connection: close
    * Server ATS/5.0.0 is not blacklisted
    < Server: ATS/5.0.0
    < Cache-Control: no-store
    < Content-Type: text/plain
    < Content-Language: en
    < Content-Length: 21
    <
    * Closing connection 0
    Authorization Denied$

#. Generate a signed URL using the included ``sign.pl`` script::

    sign.pl --url http://test-remap.domain.com/download/foo \
        --useparts 1 --algorithm 1 --duration 60 --keyindex 3 \
        --key DTV4Tcn046eM9BzJMeYrYpm3kbqOtBs7

#. Now access the protected URL with a valid signature::

    $ curl -s -o /dev/null -v --max-redirs 0 -H 'Host: test-remap.domain.com' \
        'http://test-remap.domain.com/download/foo?E=1453848506&A=1&K=3&P=1&S=7aea86592de3e9c1b05771b2538a30956c6f10a3'
    * Adding handle: conn: 0xef0a90
    * Adding handle: send: 0
    * Adding handle: recv: 0
    * Curl_addHandleToPipeline: length: 1
    * - Conn 0 (0xef0a90) send_pipe: 1, recv_pipe: 0
    * About to connect() to localhost port 8080 (#0)
    *   Trying 127.0.0.1...
    * Connected to localhost (127.0.0.1) port 8080 (#0)
    > GET /download/foo?E=1397603088&A=1&K=3&P=1&S=28d822f68ac7265db61a8441e0877a98fe1007cc HTTP/1.1
    > User-Agent: curl/7.32.0
    > Accept: */*
    > Host: test-remap.domain.com
    >
    < HTTP/1.1 200 OK
    < Location: http://www.google.com/
    < Content-Type: text/html; charset=UTF-8
    < Date: Tue, 15 Apr 2014 23:04:36 GMT
    < Expires: Thu, 15 May 2014 23:04:36 GMT
    < Cache-Control: public, max-age=2592000
    * Server ATS/5.0.0 is not blacklisted
    < Server: ATS/5.0.0
    < Content-Length: 219
    < X-XSS-Protection: 1; mode=block
    < X-Frame-Options: SAMEORIGIN
    < Alternate-Protocol: 80:quic
    < Age: 0
    < Connection: keep-alive
    <
    { [data not shown]
    * Connection #0 to host localhost left intact
