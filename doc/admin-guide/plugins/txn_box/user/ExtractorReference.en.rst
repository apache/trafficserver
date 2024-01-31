.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: ../../../../common.defs
.. include:: ../txnbox_common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _extractor_reference:

Extractor Reference
*******************

A feature is created by applying a feature expression, which consists of a mix of literal strings
and extractors.

For convenience, because a single extractor is by far the most common case, unquoted strings
are treated as a single extractor. Consider the extractor :ex:`ua-req-host`. This can be
used in the following feature expressions, presuming the host is "example.one".

============================== ==================================
Feature String                 Extracted Feature
============================== ==================================
``"Host = {ua-req-host}"``     ``Host = example.one``
``"Host = {uareq-host:*<15}"`` ``Host = example.one***``
``ua-req-host``                ``example.one``
``"ua-req-host"``              ``ua-req-host``
``"{ua-req-host}"``            ``example.one``
``"{{ua-req-host}}"``          ``{ua-req-host}``
``!literal "{ua-req-host}"``   ``{ua-req-host}``
============================== ==================================

Extractors may have or require parameters that affect what is extracted which are supplied as
an argument. This enables using these parameters inside a feature expression.

Extractors
**********

HTTP Messages
=============

There is a lot of information in the HTTP messages handled by |TS| and many extractors to access it.
These are divided in to families, each one based around one of the basic messages -

*  User Agent Request - the request sent by the user agent to |TS|.
*  Proxy Request - the request sent by |TS| (the proxy) to the upstream.
*  Upstream Response - the response sent by the upstream to |TS| in response to the Proxy Request.
*  Proxy Response - the response sent by |TS| to the user agent.

There is also the "pre-remap" or "pristine" user agent URL. This is a URL only, not a request, and
is a copy of the user agent URL just before URL rewriting.

In addition, for the remap hook, there are two other URLs (not requests) that are available. These
are the "target" and "replacement" URLs which are the values literally specified in the URL rewrite
rule. Note these can be problematic for regular expression remap rules as they are frequently not
valid URLs.

Host and Port Handling
----------------------

The host and port for a request or URL require special handling due to vagaries in the HTTP
specification. The most important distinction is these can appear in two different places, the URL
itself or in the ``Host`` field of the request, or both. This can make modifying them in a specific
way challenging. The complexity described here is to make it possible to do exactly what is needed.
In particular, although the HTTP specification says if the host and port are in the URL and in the
``Host`` header, these must be the same, in _practice_ many proxies are configured to make them
different before sending the request upstream from the proxy, generally so that the ``Host`` field
is based on what the user agent sent and the URL is changed to be where the proxy routes the
request. Therefore there are extactors which work on the request as a whole, considering both the
URL and the ``Host`` field, and others that always use the URL.

Beyond this, the port is optional and this presents some problems. One is a result of the ATS plugin
API which makes it impossible to distinguish between "http://delain.nl" and "http://delain.nl:80".
Both port 80 and port 443 are treated specially, the former for scheme "HTTP" and the latter for
"HTTPS". I intend to add that at some point but currently it cannot be done. The result is it is
difficult to impossible to properly set these values in a configuration language such as |TxB| has
and even if possible would be rather painful to do repeatedly. Therefore |TxB| has the concept of
"location" which corresponds to the host and port (the HTTP specification calls this the `authority
<https://tools.ietf.org/html/rfc3986#section-3.2>`__ but everyone thought using the term was a
terrible idea). This makes it easy to access the host, the port, or both. This is more important
when interacting with directives to set those values. Here is a chart to illustrate the three terms

============================== ==================== ==== =====================
url                             host                port loc
============================== ==================== ==== =====================
http://evil-kow.ex/path        evil-kow.ex          80    evil-kow.ex
http://evil-kow.ex/path:80     evil-kow.ex          80    evil-kow.ex
https://evil-kow.ex/path       evil-kow.ex          443   evil-kow.ex
http://evil-kow.ex:4443/path   evil-kow.ex          4443  evil-kow.ex:4443
https://evil-kow.ex:4443/path  evil-kow.ex          4443  evil-kow.ex:4443
============================== ==================== ==== =====================

Paths
-----

Unfortunately due to how the plugin API works, paths are a bit odd. One result is

.. important:: Paths do not have a leading slash

Given the URL "http://delain.nl/pix", the path is "pix", not "/pix". The existence of the slash is
implied by the existence of the path. There is, unfortunately, no way to distinguish a missing from
an empty path. E.g. "http://delain.nl" and "http://delain.nl/" are not distinguishable by looking at
the value from a path extractor, both will yield an empty string. This matters less than it appears
because both ATS and the upstream will treat them identically. Note this applies only to the slash
separating the "authority" / "location" from the path. The path for the URLs
"http://delain.nl/pix/charlotte" and "http://delain.nl/pix/charlotte/" are distinguishable.

* :ref:`ex-user-agent`
* :ref:`ex-pre-remap`
* :ref:`ex-rewrite-rule-urls`
* :ref:`ex-proxy-request`
* :ref:`ex-upstream-response`
* :ref:`ex-proxy-response`
* :ref:`ex-transaction`
* :ref:`ex-session`
* :ref:`ex-duration`
* :ref:`ex-utility`

.. _ex-user-agent:

User Agent Request
------------------

.. extractor:: ua-req-method
   :result: string

   The user agent request method.

.. extractor:: ua-req-url
   :result: string

   The URL in the user agent request.

.. extractor:: ua-req-scheme
   :result: string

   The URL scheme in the user agent request.

.. extractor:: ua-req-loc
   :result: string

   The location for the request, consisting of the host and the optional port. This is retrieved
   from the URL if present, otherwise from the ``Host`` field.

.. extractor:: ua-req-host
   :result: string

   Host for the user agent request. This is retrieved from the URL if present, otherwise from the
   ``Host`` field. This does not include the port.

.. extractor:: ua-req-port
   :result: integer

   The port for the user agent request. This is pulled from the URL if present, otherwise from
   the ``Host`` field. If not specified, the canonical default based on the scheme is used.

.. txb:extractor:: ua-req-path
   :result: string

   The path of the URL in the user agent request. This does not include a leading slash.

.. extractor:: ua-req-query
   :result: string

   The query string for the user agent request if present, an empty string if not.

.. extractor:: ua-req-query-value
   :result: string
   :arg: Query parameter key.

   The value for a specific query parameter, identified by key. This assumes the standard format for
   a query string, key / value pairs (joined by '=') separated by '&' or ';'. The key comparison
   is case insensitive. ``NIL`` is returned if the key is not found.

.. extractor:: ua-req-fragment
   :result: string

   The fragment of the URL in the user agent request if present, an empty string if not.

.. extractor:: ua-req-url-host
   :result: string

   Host for the user agent request URL.

.. extractor:: ua-req-url-port
   :result: integer

   The port for the user agent request URL.

.. extractor:: ua-req-url-loc
   :result: string

   The location for the user agent request URL, consisting of the host and the optional port.

.. extractor:: ua-req-field
   :result: NULL, string, string list
   :arg: name

   The value of a field in the client request. This requires a field name as a argument. To
   get the value of the "Host" field the extractor would be "ua-req-field<Host>". The field name is
   case insensitive.

   If the field is not present, the ``NULL`` value is returned. Note this is distinct from the
   empty string which is returned if the field is present but has no value. If there are duplicate
   fields then a string list is returned, each element of which corresponds to a field.

.. _ex-pre-remap:

Pre-Remap
~~~~~~~~~

   The following extractors extract data from the user agent request URL, but from the URL as it was
   before URL rewriting ("remapping"). Only the URL is preserved, not any of the fields or the
   method. These are referred to elsewhere as "pristine" but that is a misnomer. If the user agent
   request is altered before URL rewriting, that will be reflected in the data from these
   extractors. These do not necessarily return the URL as it was received by ATS from the user
   agent. All of these have an alias with "pristine" instead of "pre-remap" for old school
   operations staff. There are no directives to modify these values, they are read only.

.. extractor:: pre-remap-scheme
   :result: string

      The URL scheme in the pre-remap user agent request URL.

.. extractor:: pre-remap-url
   :result: string

      The full URL of the pre-remap user agent request.

.. txb:extractor:: pre-remap-path
   :result: string

      The URL path in the pre-remap user agent request URL. This does not include a leading slash.

.. txb:extractor:: pre-remap-host
   :result: string

      The host in the pre-remap user agent request URL. This does not include the port.

.. extractor:: pre-remap-port
   :result: integer

      The port in the pre-remap user agent request URL. If not specified, the canonical default based
      on the scheme is used.

.. extractor:: pre-remap-query
   :result: string

      The query string for the pre-remap user agent request URL.

.. extractor:: pre-remap-query-value
   :result: string
   :arg: Query parameter key.

   The value for a specific query parameter, identified by key. This assumes the standard format for
   a query string, key / value pairs (joined by '=') separated by '&' or ';'. The key comparison
   is case insensitive. ``NIL`` is returned if the key is not found.

.. extractor:: pre-remap-fragment
   :result: string

   The fragment of the URL in the pre-remap user agent request if present, an empty string if not.

.. _ex-rewrite-rule-urls:

Rewrite Rule URLs
-----------------

   During URL rewriting there are two additional URLs available, `the "target" and the "replacement"
   URL
   <https://docs.trafficserver.apache.org/en/latest/admin-guide/files/remap.config.en.html#format>`__.
   These are fixed values from the rule itself, not the user agent. For this reason there are
   extractors to get data from these URLs but no directives to modify them. These values are
   available only for the "remap" hook, that is directives invoked from a rule in "remap.config".
   Query values are not permitted in these URLs and so no extractor for that is provided.

.. extractor:: remap-target-url
   :result: string

   The full target URL.

.. extractor:: remap-target-scheme
   :result: string

   The scheme in the target URL.

.. extractor:: remap-target-loc
   :result: string

   The network location of the target URL.

.. txb:extractor:: remap-target-host
   :result: string

   The host in the target URL. This does not include the port, if any.

.. extractor:: remap-target-port
   :result: integer

   The port in the target URL. If not specified, the default based on the scheme is extracted.

.. extractor:: remap-target-path
   :result: string

   The path in the target URL.

.. extractor:: remap-replacement-url
   :result: string

   The full replacement URL.

.. extractor:: remap-replacement-scheme
   :result: string

   The scheme in the replacement URL.

.. extractor:: remap-replacement-loc
   :result: string

   The network location in the replacement URL.

.. txb:extractor:: remap-replacement-host
   :result: string

   The host in the replacement URL. This does not include the port, if any.

.. extractor:: remap-replacement-port
   :result: integer

   The port in the replacement URL. If not specified, the default based on the scheme is extracted.

.. extractor:: remap-replacement-path
   :result: string

   The path in the replacement URL.

.. _ex-proxy-request:

Proxy Request
-------------

.. extractor:: proxy-req-method
   :result: string

   The proxy request method.

.. extractor:: proxy-req-url
   :result: string

   The URL in the request.

.. extractor:: proxy-req-scheme
   :result: string

   The URL scheme in the proxy request.

.. extractor:: proxy-req-loc
   :result: string

   The network location in the request. This is retrieved from the URL if present, otherwise from
   the ``Host`` field.

.. extractor:: proxy-req-host
   :result: string

   Host for the request. This is retrieved from the URL if present, otherwise from the ``Host``
   field. This does not include the port.

.. txb:extractor:: proxy-req-path
   :result: string

   The path of the URL in the request. This does not include a leading slash.

.. extractor:: proxy-req-port
   :result: integer

   The port for the request. This is pulled from the URL if present, otherwise from the ``Host``
   field.

.. extractor:: proxy-req-query
   :result: string

   The query string in the proxy request.

.. extractor:: proxy-req-query-value
   :result: string
   :arg: Query parameter key.

   The value for a specific query parameter, identified by key. This assumes the standard format for
   a query string, key / value pairs (joined by '=') separated by '&' or ';'. The key comparison
   is case insensitive. ``NIL`` is returned if the key is not found.

.. extractor:: proxy-req-fragment
   :result: string

   The fragment of the URL in the proxy request if present, an empty string if not.

.. extractor:: proxy-req-url-host
   :result: string

   The host in the request URL.

.. extractor:: proxy-req-url-port
   :result: integer

   The port in the request URL.

.. extractor:: proxy-req-url-loc
   :result: string

   The location in the URL if present, an empty string if not.

.. extractor:: proxy-req-field
   :result: NULL, string, string list
   :arg: name

   The value of a field. This requires a field name as a argument. To get the value of the "Host"
   field the extractor would be "proxy-req-field<Host>". The field name is case insensitive.

   If the field is not present, the ``NULL`` value is returned. Note this is distinct from the
   empty string which is returned if the field is present but has no value. If there are duplicate
   fields then a string list is returned, each element of which corresponds to a field.

.. _ex-upstream-response:

Upstream Response
-----------------

.. extractor:: upstream-rsp-status
   :result: integer

   The code of the response status.

.. extractor:: upstream-rsp-status-reason
   :result: string

   The reason of the response status.

.. extractor:: upstream-rsp-field
   :result: NULL, string, string list
   :arg: name

   The value of a field. This requires a field name as a argument. The field name is case
   insensitive.

   If the field is not present, the ``NULL`` value is returned. Note this is distinct from the
   empty string which is returned if the field is present but has no value. If there are duplicate
   fields then a string list is returned, each element of which corresponds to a field.

.. _ex-proxy-response:

Proxy Response
--------------

.. extractor:: proxy-rsp-status
   :result: integer

   The code of the response status.

.. extractor:: proxy-rsp-status-reason
   :result: string

   The reason of the response status.

.. extractor:: proxy-rsp-field
   :result: NULL, string, string list
   :arg: name

   The value of a field. This requires a field name as a argument. The field name is case
   insensitive.

   If the field is not present, the ``NULL`` value is returned. Note this is distinct from the
   empty string which is returned if the field is present but has no value. If there are duplicate
   fields then a string list is returned, each element of which corresponds to a field.

.. _ex-transaction:

Transaction
===========

.. extractor:: is-internal
   :result: boolean

   This returns a boolean value, ``true`` if the request is an internal request, and ``false`` if not.

.. _ex-session:

Session
=======

.. extractor::  inbound-txn-count
   :result: integer

   The number of transactions, including the current on, that have occurred on the inbound
   transaction.

.. extractor:: inbound-addr-remote
   :result: IP address

   The remote address for the inbound connection. This is also known as the "client address", the
   address from which the connection originates.

.. extractor:: inbound-addr-local
   :result: IP address

   The local address for the inbound connection, which is the address used accept the inbound session.

.. txb:extractor:: inbound-sni
   :result: string

   The SNI name sent on the inbound session.

.. extractor:: has-inbound-protocol-prefix
   :result: boolean
   :arg: protocol tag prefix

   For the inbound session there is a `list of protocol tags
   <https://docs.trafficserver.apache.org/en/latest/developer-guide/api/functions/TSClientProtocolStack.en.html>`__
   that describe the network protocols used for that network connection. This extractor checks the
   inbound session list to see if it contains a tag that has a specific prefix. The most common use
   is to determine if the inbound session is TLS ::

      with: has-inbound-protocol-prefix<tls>
      select:
      -  is-true:
         do: # TLS only stuff.

   .. note::

      Checking a request for the scheme "https" is not identical to checking for TLS. Nothing
      prevents a user agent from sending a scheme at variance with the network protocol stack. This
      extractor checks the network protocol, not the request.

   Checking for IPv6 can be done in a similar way. ::

      with: has-inbound-protocol-prefix<ipv6>
      select:
      - is-true:
        do: # IPv6 special handling.

.. extractor:: inbound-protocol
   :result: string
   :arg: protocol tag prefix

   For the inbound session there is a `list of protocol tags
   <https://docs.trafficserver.apache.org/en/latest/developer-guide/api/functions/TSClientProtocolStack.en.html>`__
   that describe the network protocols used for that network connection. This extractor searches the
   inbound session list and if there is a prefix match, returns the matched protocol tag. This can
   be used to check for different versions of TLS. ::

      with: inbound-protocol<tls>
      select:
      -  match: "tls/1.3"
         do: # TLS 1.3 only stuff.
      -  prefix: "tls"
         do: # Older TLS stuff.
      -  otherwise:
         do: # Non-TLS stuff.

.. extractor:: inbound-protocol-stack
   :result: tuple of strings

   This extracts the entire stack of tags for the network protocols of the inbound connection as a
   tuple. This could be used to check for an IPv4 connection ::

      with: inbound-protocol-stack
      select:
      -  for-any:
            match: "ipv4"
         do:
         # IPv4 only things.

   In general, though, :ex:`has-inbound-protocol-prefix` is usually a better choice for doing such
   checking unless the full stack or a full tag is needed.

.. extractor:: inbound-cert-verify-result
   :result: integer

   The result of verifying the inbound remote (client) certificate. Due to issues in the OpenSSL
   library this can be a bit odd. If the the inbound session is not TLS the result will be
   ``X509_V_ERR_INVALID_CALL`` which as of this writing has the value 69 (:`reference
   <https://www.openssl.org/docs/man1.1.0/man1/verify.html>`__). Otherwise, if no client certificate
   was provided and was not required the result is ``X509_V_OK`` which has the value 0. This lack
   can be detected indirectly by all of the certificate extractors returning empty strings.

.. extractor:: inbound-cert-local-issuer-field
   :result: string
   :arg: Entry name.

   Extract the value for an entry in the local (ATS) certificate issuer for an inbound session.
   This will accept a short or long name as the argument. Note these names are case sensitive.

.. extractor:: inbound-cert-local-subject-field
   :result: string
   :arg: Entry name.

   Extract the value for an entry in the local (ATS) certificate subject for an inbound session.
   This will accept a short or long name as the argument. Note these names are case sensitive.

.. extractor:: inbound-cert-remote-issuer-field
   :result: string
   :arg: Entry name.

   Extract the value for an entry in the remote (client) certificate issuer for an inbound session.
   This will accept a short or long name as the argument. Note these names are case sensitive.

   If a client certificate wasn't provided or failed validation, this will yield an empty string.

.. extractor:: inbound-cert-remote-subject-field
   :result: string
   :arg: Entry name.

   Extract the value for an entry in the remote (client) certificate subject for an inbound session.
   This will accept a short or long name as the argument. Note these names are case sensitive.

   If a client certificate wasn't provided or failed validation, this will yield an empty string.

.. extractor:: outbound-cert-local-issuer-field
   :result: string
   :arg: Entry name.

   Extract the value for an entry in the local (ATS) certificate issuer for an outbound session.
   This will accept a short or long name as the argument. Note these names are case sensitive.

.. extractor:: outbound-cert-local-subject-field
   :result: string
   :arg: Entry name.

   Extract the value for an entry in the local (ATS) certificate subject for an outbound session.
   This will accept a short or long name as the argument. Note these names are case sensitive.

.. extractor:: outbound-cert-remote-issuer-field
   :result: string
   :arg: Entry name.

   Extract the value for an entry in the remote (server) certificate issuer for an outbound session.
   This will accept a short or long name as the argument. Note these names are case sensitive.

   If the destination didn't provide a certificate or failed validation, this will yield an empty string.

.. extractor:: outbound-cert-remote-subject-field
   :result: string
   :arg: Entry name.

   Extract the value for an entry in the remote (server) certificate subject for an outbound session.
   This will accept a short or long name as the argument. Note these names are case sensitive.

   If the destination didn't provide a certificate or failed validation, this will yield an empty string.

.. extractor::  outbound-txn-count
   :result: integer

   The number of transactions between the Traffic Server proxy and the origin server from a single session.
   Any value greater than zero indicates connection reuse.

   .. code-block:: yaml

      with: outbound-txn-count
      select:
      - gt: 10
        do:
         - proxy-rsp-field<Connection>: "close"

.. warning::
   For ATS versions before 10, this will return `0` and the value should not be taken
   into consideration to determine connection reuse.

.. extractor:: outbound-addr-remote
   :result: IP address

   The address of the origin server for a transaction.

.. extractor:: outbound-addr-local
   :result: IP address

   The local address of the server connection for a transaction.

.. _ex-duration:

Duration
========

A "duration" is a span of time. This is specified by one of a set of extractors.

.. extractor:: milliseconds
   :arg: count
   :result: duration

   A duration of :arg:`count` milliseconds.

.. extractor:: seconds
   :arg: count
   :result: duration

   A duration of :arg:`count` seconds.

.. extractor:: minutes
   :arg: count
   :result: duration

   A duration of :arg:`count` minutes.

.. extractor:: hours
   :arg: count
   :result: duration

   A duration of :arg:`count` hours.

.. _ex-utility:

Utility
=======

This is an ecletic collection of extractors that do not depend on transaction or session data.

.. extractor:: ...
   :result: any

   The feature for the most recent :drtv:`with`.

.. txb:extractor:: random
  :result: integer

   Generate a random integer in a uniform distribution. The default range is 0..99 because the most
   common use is for a percentage. This can be changed by adding arguments. A single number argument
   changes the upper bound. Two arguments changes the range. E.g.

   :code:`random<199>` generates integers in the range 0..199.

   :code:`random<1,100>` generates integers in the range 1..100.

   The usual style for using this in a percentage form is ::

      with: random
      select:
      - lt: 5 # match 5% of the time
        do: # ...
      - lt: 25: # match 20% of the time - 25% less the previous 5%
        do: # ...

.. extractor:: text-block
   :arg: name
   :result: string

   Extract the content of the text block (defined by a :drtv:`text-block-define`) for :arg:`name`.

.. extractor:: ip-col
   :arg: Column name or index

   This must be used in the context of the modifier :mod:`ip-space` which creates the row context
   needed to extract the column value for that row. The argument can be the name of the column, if
   it has a name, or the index. Note index 0 is the IP address range, and data columns start at
   index 1.

.. extractor:: stat
   :arg: Plugin statistic name.
   :result: integer

   This extracts the value of a plugin statistic, which is currently limited to integers by |TS|.

   Note statistic values are eventually consistent, there can be multiple second delays between
   incrementing a statistic with :drtv:`stat-update` and the value changing.

.. extractor:: env
   :arg: Variable name
   :result: string

   Extract the value of the named variable from the process environment.

.. extractor:: inbound-tcp-info
   :arg: Field name
   :result: integer

   Extracts a field value from the `tcp_info <https://man7.org/linux/man-pages/man7/tcp.7.html>`__
   data available on some operating systems. If not available, ``NULL`` is returned.

   The currently supported fields are

   rtt
      Round trip time.

   rto
      Retransmission timeout.

   retrans
      Retransmits.

   snd-cwnd
      Outbound congestion window.

   .. note:

      These fields are poorly documented, the general recommendation being "read the kernel code"
      which seems a bit terse. Use with caution.

.. extractor:: ts-uuid
   :result: string

   The process level UUID for this instance of |TS|.
