.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: ../../../common.defs
.. include:: txnbox_common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _examples:

********
Examples
********

|TxB| is a large, complex plugin and it can be challenging to get started. This section provides a
number of example uses, all of which are based on actual production use of |TxB|.

Default Accept-Encoding
=======================

Goal
   Force all proxy requests to have a value for the "Accept-Encoding" field. If not already set, it
   should be set to "identity".

.. literalinclude:: ../../../../tests/gold_tests/pluginTest/txn_box/example/accept-encoding.replay.yaml
   :start-after: doc.start
   :end-before: doc.end

This acts on the proxy request hook. The "Accept-Encoding" field is extracted and then modified
using the :mod:`else` which modifies the feature only if the feature is null or the empty string.

Traffic Ramping
===============

For the purposes of this example, there is presumed to exist a remap rule for a specific externally
visible host, "base.ex". A new version is being staged on the host "stage.video.ex". The goal is to
redirect a fixed percentage of traffic from the existing host to the staging host, in way that is
easy to change. In addition it should be easy to have multiple ramping values for different URL
paths. The paths for the two hosts are identical, only the host for the request needs to be changed.

The simplest way to do this would be

.. literalinclude:: ../../../../tests/gold_tests/pluginTest/txn_box/ramp/multi-ramp-1.cfg.yaml
   :start-after: doc.start
   :end-before: doc.end

This has two buckets, the first at 30% and the second at 10%. :ex:`random` is used to generate
random numbers in the range 0..99 which means the extracted value is :code:`lt: 30` roughly 30 times
out of every hundred, or 30% of the time. The buckets are selected by first checking the pre-remap
path (so that it is not affected by other plugins which may run earlier in remapping). Two paths
are in the 30% bucket and one in the 10% bucket. Adding additional paths is easy, as is changing
the percent diversion. Other buckets can be added with little effort.

This can be done in another way by generating the random value once and checking it multiple times.
Given the no backtrack rule, this is challenging to do by checking the percentage first. Instead the
use of tuples makes it possible to check both the value and the path together.

.. literalinclude:: ../../../../tests/gold_tests/pluginTest/txn_box/ramp/multi-ramp-2.cfg.yaml
   :start-after: doc.start
   :end-before: doc.end

The :drtv:`with` is provided with a tuple of size 2, the random value and the pre-remap path. Each
comparison uses :cmp:`as-tuple` to perform parallel comparisons on the tuple. The first comparison
is applied to the first tuple element, the value, and the second comparison to the second value, the
path. Because there is no nested :drtv:`with` there is no need to backtrack.

It might be reasonable to split every path in to a different bucket to make adjusting the percentage
easier. In that case the previous example could be changed to look like

.. literalinclude:: ../../../../tests/gold_tests/pluginTest/txn_box/ramp/multi-ramp-3.cfg.yaml
   :lines: 1-12,16-17

This style presumes the bucket action is identical for all buckets. If not, the previous style would
be better. Note the :code:`do` is attached to the :cmp:`any-of` so that if any of the nested comparisons
succeed the action is performed.

Static File Serving
===================

|TxB| enables serving defined content. This can be done with the :drtv:`upstream-rsp-body` directive
to replace content from the upstream with configuration specified content. This is enhanced with
"text blocks" which allow obtaining content from external files.

Goal
   Provide a default `security.txt file <https://securitytxt.org>`__ if an upstream doesn't provide
   one.

Example configuration

.. literalinclude:: ../../../../tests/gold_tests/pluginTest/txn_box/static_file/static_file.replay.yaml
   :start-after: doc-1-->
   :end-before: doc-1--<

This checks on the upstream response. If the status is 404 (not found) and the path is exactly
"security.txt" then change the response to a 200 and provide a hard wired default for the content.
The text is retrieved via a YAML reference to an anchor.

.. literalinclude:: ../../../../tests/gold_tests/pluginTest/txn_box/static_file/static_file.replay.yaml
   :start-after: doc-secure-text-->
   :end-before: doc-secure-text--<

Unfortunately this is insufficient because in some situations there will be no proxy request and
therefore no upstream response. Because of limitations in the plugin API this can't be handled
directly and requires an additional bit of configuration for that case. This is the reason for using
the anchor and reference in the previous configuration, to make sure the exact same text is used in
both cases. Note this is done during YAML parsing, not at runtime, and is identical to using literal
strings in both cases.

.. literalinclude:: ../../../../tests/gold_tests/pluginTest/txn_box/static_file/static_file.replay.yaml
   :start-after: doc-proxy-rsp-->
   :end-before: doc-proxy-rsp--<

Note only one of these will trigger on a specific transaction, because if the upstream check
activates, the status will not be 404 when it is checked here. This is also a bit more complex
because in some situations for which this is checking the proxy request will not have been created,
e.g. if there is a failure to remap the request. As a result the path uses a modifier so that if
:ex:`proxy-req-path` isn't available due to the proxy request not having been created, it falls back
to :ex:`ua-req-path` to get the path the user agent sent. It would also be reasonable to only use
:ex:`ua-req-path` in both cases so that only if the user agent specifically requested "security.txt"
would the default be used.

Goal
   Provide a default `JSON web token <https://tools.ietf.org/html/rfc7519>`__.

The utility here is to bootstrap into an established JWT infrastructure. On a first request into a
CDN the default token is provided to enable access to the resources needed to get a personalized
token. For security reasons the tokens expire on a regular basis which includes the default token.
It would be too expensive to restart or reload |TS| on every expiration. Presuming an infrastructure
that pushes default tokens to the file "/var/www/jwt/default-token.jwt", a text block can be defined
to load that file and check it for changes every 12 hours. If the file is missing, a special marker
"N/A" that signals this problem to the upstream.

.. literalinclude:: ../../../../tests/gold_tests/pluginTest/txn_box/static_file/static_file.replay.yaml
   :start-after: doc-jwt-->
   :end-before: doc-jwt--<

To use this, the proxy request is checked for the "Author-i-tay" field. If set it is passed through
on the presumption it is a valid token. If not, then the default token is added.

.. literalinclude:: ../../../../tests/gold_tests/pluginTest/txn_box//static_file/static_file.replay.yaml
   :start-after: doc-jwt-apply-->
   :end-before: doc-jwt-apply--<

Once this is set up, pushes of new tokens to the file system on the production system takes no more
than 12 hours to show up in the default tokens used.

.. _example-path-tweaking:

Path Tweaking
=============

This example also serves to illustrate the use of :code:`continue` in :drtv:`with`. The goal is to
adjust a file name in a path depending on the presence of specific query parameters, which are then
discarded. If the parameter "ma=0" is present, then the base file name must have the string "_noma"
attached. If the parameter "mc=1" is present, then the base filename must have the string "_mac"
attached. If both are present then both strings are added.

.. literalinclude:: ../../../../tests/gold_tests/pluginTest/txn_box/prod/vznith-1.replay.yaml
   :start-after: doc-start
   :end-before: doc-end

The configuration uses :drtv:`with` to check for the query parameters and adjust a variable
containing the file name as needed. The :code:`continue` causes the invocation to proceed past the
:drtv:`with` even if it matches. At the end, the path is assembled and set and the query parameters
cleared.

Client Certificate Authorization
================================

A common use of |TS| is to place it as a "side-car" in front of a service to perform various network
tasks. The task in this example is to use client certificate information to authorize access to the
proxied service. We presume client certificates are issued to provide authentication and
authorization (such as via a tool like `Athenz <https://athenz.io>`__) but it would be too much
work to change the service to verify this. Instead the service can use `HTTP basic
authentication <https://datatracker.ietf.org/doc/html/rfc7617>`__ and have |TS| provide that
authentication based on information in the client certificate while scrubbing the inbound request to
prevent spoofing.

This requires additional support from |TS| to requires a client certificate and verify it is signed
by a trusted root certificate. To do this globally, add the `configuration value
<https://docs.trafficserver.apache.org/en/9.1.x/admin-guide/files/records.config.en.html#proxy-config-ssl-client-certification-level>`__
to `records.config
<https://docs.trafficserver.apache.org/en/9.1.x/admin-guide/files/records.config.en.html#std-configfile-records.config>`__
::

   CONFIG proxy.config.ssl.client.certification_level INT 2

The trusted root certificates are either the base operating system certificates or those specified
by the configuration variables
`proxy.config.ssl.CA.cert.path <https://docs.trafficserver.apache.org/en/9.1.x/admin-guide/files/records.config.en.html#proxy-config-ssl-client-cert-path>`__
and
`proxy.config.ssl.CA.cert.filename <https://docs.trafficserver.apache.org/en/9.1.x/admin-guide/files/records.config.en.html#proxy-config-ssl-client-cert-filename>`__

The basic setup is to check the values on the client certificate and set the ``Authorization`` field
if valid and remove it if not.

.. literalinclude:: ../../../../tests/gold_tests/pluginTest/txn_box/prod/mTLS.txnbox.yaml

This checks two values - the issuer (authentication) and the subject field (authorization).

*  The issuer must be exactly the expected issuer.
*  Authorization is passed in the subject field, which is expected to contain
   an authorization domain ("base.ex"), a key word ("role") and then the actual authorization role.

If both are successful then the role for the request is extracted and passed to the service in
the ``Authorization`` field. Otherwise only ``GET`` and ``HEAD`` methods are allowed - if neither of
those the request is immediately rejected with a 418 status. If allowed the ``Authorization`` field
is stripped so the service can detect the lack of authorization for the request.

Pulling the authorization value from the certificate disconnects the |TS| configuration from the
specific roles supported by the service. Whatever those roles are, the administrator can put them
in the certificate where they can be passed through.

While it is best to adjust `remap.config
<https://docs.trafficserver.apache.org/en/9.1.x/admin-guide/files/remap.config.en.html>`__ to not
forward non-TLS requests, this configuration will still work correctly because the
client certificate values will be ``NIL`` for a plain text connection and therefore not match. |TS|
support is needed for the TLS case to verify the client certificate so |TxB| can trust the values
pulled from that certificate.

If this is needed in a multi-tenant / CDN proxy, it will be necessary to use `sni.yaml
<https://docs.trafficserver.apache.org/en/9.1.x/admin-guide/files/sni.yaml.en.html>`__ to adjust the
client certificate requirements based on the SNI.
