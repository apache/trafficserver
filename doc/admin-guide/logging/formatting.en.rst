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

.. _admin-logging-formats:

Formatting Log Files
********************

This section covers the creation of logging formats. All but a few logging
output related settings in |TS| are performed in :file:`logging.yaml` and
consulting the documentation for that file is recommended in addition to this
section. Any configurations or settings performed outside that file will be
clearly noted.

.. _admin-logging-defining-format:

Defining Formats
================

Logging formats in |TS| are defined by editing :file:`logging.yaml`
and adding new format entries for each format you wish to define. The syntax is
fairly simple: every format must contain a ``Format`` attribute, which is the
string defining the contents of each line in the log, and may also contain an
optional ``Interval`` attribute defining the log aggregation interval for
any logs which use the format (see :ref:`admin-logging-type-summary` for more
information).

The return value from the ``format`` function is the log format object which
may then be supplied to the appropriate ``log.*`` functions that define your
logging destinations.

A very simple example, which contains only the timestamp of when the event began
and the canonical URL of the request, would look like:

.. code:: yaml

   formats:
   - name: myformat
     format: '%<cqtq> %<cauc>'


You may include as many custom field codes as you wish. The full list of codes
available can be found in :ref:`admin-logging-fields`. You may also include
any literal characters in your format. For example, if we wished to separate
the timestamp and canonical URL in our customer format above with a slash
instead of a space, or even a slash surrounded by spaces, we could do so by
just adding the desired characters to the format string:

.. code:: yaml

   formats:
   - name: myformat
     format: '%<cqtq> / %<cauc>'

You may define as many custom formats as you wish. To apply changes to custom
formats, you will need to run the command :option:`traffic_ctl config reload`
after saving your changes to :file:`logging.yaml`.

.. _admin-logging-fields:

Log Fields
==========

The following sections detail all the available |TS| logging fields, broken
down into the following broad categories for (hopefully) easier reference:

- :ref:`admin-logging-fields-auth`
- :ref:`admin-logging-fields-cache`
- :ref:`admin-logging-fields-txn`
- :ref:`admin-logging-fields-content-type`
- :ref:`admin-logging-fields-hierarchy`
- :ref:`admin-logging-fields-headers`
- :ref:`admin-logging-fields-methods`
- :ref:`admin-logging-fields-ids`
- :ref:`admin-logging-fields-lengths`
- :ref:`admin-logging-fields-collation`
- :ref:`admin-logging-fields-network`
- :ref:`admin-logging-fields-plugin`
- :ref:`admin-logging-fields-proto`
- :ref:`admin-logging-fields-request`
- :ref:`admin-logging-fields-ssl`
- :ref:`admin-logging-fields-status`
- :ref:`admin-logging-fields-tcp`
- :ref:`admin-logging-fields-time`
- :ref:`admin-logging-fields-urls`

Individual log fields are used within a log format string by enclosing them in
angle brackets and prefixing with a percent sign. For example, to use the log
field cqhl_ (the length in bytes of the client request headers), you
would do the following::

    Format = '%<cqhl>'

Literal characters may be used, but they must be outside the log fields'
placeholders, as so::

    Format = 'Client Header Length (bytes): %<cqhl>'

You may combine many fields into a single format string (logs wouldn't be very
useful if you couldn't). Some fields do require a little extra treatment, which
is noted clearly in their descriptions below. This affects, primarily, those
fields which provide access to HTTP header values as you need to specify which
header's value you wish to appear in the log data. For these, the header name
is noted inside the angle brackets, before the log field name, and are
enclosed within a curly braces pair. For example, to include the value of the
Age header from an origin server response you would do::

    Format = '%<{Age}ssh>'

.. _admin-logging-fields-auth:

Authentication
~~~~~~~~~~~~~~

.. _caun:

These log fields provide access to various details of a client or proxy's
means of request authentication to their destination (whether it be the client
request to a proxy server, or the proxy server's request to an origin).

===== ============== ==========================================================
Field Source         Description
===== ============== ==========================================================
caun  Client Request Authentication User name as a result of the RFC931/ident
                     lookup for the client-provided name.
===== ============== ==========================================================

.. _admin-logging-fields-cache:

Cache Details
~~~~~~~~~~~~~

.. _cluc:
.. _crc:
.. _crsc:
.. _chm:
.. _cwr:
.. _cwtr:

These log fields reveal details of the |TS| proxy interaction with its own
cache while attempting to service incoming client requests.

===== ============== ==========================================================
Field Source         Description
===== ============== ==========================================================
cluc  Client Request Cache Lookup URL, also known as the :term:`cache key`,
                     which is the canonicalized version of the client request
                     URL.
crc   Proxy Cache    Cache Result Code. The result of |TS| attempting to obtain
                     the object from cache; :ref:`admin-logging-cache-results`.
crsc  Proxy Cache    Cache Result Sub-Code. More specific code to complement the
                     Cache Result Code.
chm   Proxy Cache    Cache Hit-Miss status. Specifies the level of cache from
                     which this request was served by |TS|. Currently supports
                     only RAM (``HIT_RAM``) vs disk (``HIT_DISK``).
cwr   Proxy Cache    Cache Write Result. Specifies the result of attempting to
                     write to cache: not relevant (``-``), no cache write
                     (``WL_MISS``), write interrupted (``INTR``), error while
                     writing (``ERR``), or cache write successful (``FIN``).
cwt   Proxy Cache    Cache Write Transform Result.
===== ============== ==========================================================

.. _admin-logging-fields-txn:

Connections and Transactions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _sca:
.. _sstc:
.. _ccid:
.. _ctid:

The following log fields are used to list various details of connections and
transactions between |TS| proxies and origin servers.

===== ============== ==================================================================
Field Source         Description
===== ============== ==================================================================
sca   Proxy          Number of attempts within the current transaction by |TS|
                     in connecting to the origin server.
sstc  Proxy          Number of transactions between the |TS| proxy and the origin
                     server from a single session. Any value greater than zero
                     indicates connection reuse.
ccid  Client Request Client Connection ID, a non-negative number for a connection,
                     which is different for all currently-active connections to
                     clients.
ctid  Client Request Client Transaction ID, a non-negative number for a transaction,
                     which is different for all currently-active transactions on the
                     same client connection.  For client HTTP/2 transactions, this
                     value is the stream ID for the transaction.
===== ============== ==================================================================

.. _admin-logging-fields-content-type:

Content Types
~~~~~~~~~~~~~

.. _psct:

Log fields used to reveal the HTTP content types in effect for transactions.

===== ====================== ==================================================
Field Source                 Description
===== ====================== ==================================================
psct  Origin Server Response Content type of the document obtained by |TS| from
                             the origin server response.
===== ====================== ==================================================

.. _admin-logging-fields-error-code:

Error Code
~~~~~~~~~~

.. _crec:
.. _ctec:

The log fields of error code which is triggered session close or
transaction close. The first byte of this field indicates that the error
code is session level (``S``) or transaction level (``T``).
When no error code is received or transmitted, these fields are ``-``.
For HTTP/2, error code are described in RFC 7540 section 7.

===== =============== =========================================================
Field Source          Description
===== =============== =========================================================
crec  Client Request  Error code in hex which |TS| received
ctec  Client Response Error code in hex which |TS| transmitted
===== =============== =========================================================

.. _admin-logging-fields-hierarchy:

Hierarchical Proxies
~~~~~~~~~~~~~~~~~~~~

.. _phr:

The log fields detail aspects of transactions involving hierarchical caches.

===== ====== ==================================================================
Field Source Description
===== ====== ==================================================================
phr   Proxy  Proxy Hierarchy Route. Specifies the route through configured
             hierarchical caches used to retrieve the object.
===== ====== ==================================================================

.. _admin-logging-fields-headers:

HTTP Headers
~~~~~~~~~~~~

.. _cqh:
.. _pqh:
.. _psh:
.. _ssh:
.. _cssh:
.. _ecqh:
.. _epqh:
.. _epsh:
.. _essh:
.. _ecssh:
.. _cqah:
.. _pqah:
.. _psah:
.. _ssah:
.. _cssah:

The following log tables provide access to the values of specified HTTP headers
from each phase of the transaction lifecycle. Unlike many of the other log
fields, these require a little extra notation in the log format string, so that
|TS| knows the individual HTTP header from which you aim to extract a value for
the log entry.

This is done by specifying the name of the HTTP header in curly braces, just
prior to the log field's name, as so::

    Format = '%<{User-agent}cqh>'

The above would insert the User Agent string from the client request headers
into your log entry (or a blank string if no such header was present, or it did
not contain a value).

===== ====================== ==================================================
Field Source                 Description
===== ====================== ==================================================
cqh   Client Request         Logs the value of the named header from the
                             client's request to the |TS| proxy.
pqh   Proxy Request          Logs the value of the named header from the |TS|
                             proxy's request to the origin server.
psh   Proxy Response         Logs the value of the named header from the |TS|
                             proxy's response to the client.
ssh   Origin Response        Logs the value of the named header from the origin
                             server's response to the proxy.
cssh  Cached Origin Response Logs the value of the named header from the
                             *cached* origin server response.
===== ====================== ==================================================

Each of these also includes a URI-encoded variant, which replaces various
characters in the string with entity encodings - rendering them safe for use in
URL path components or query parameters. The variants' names follow the pattern
of the origin field named prefixed with ``e``, as shown here:

============== ===================
Original Field URL-Encoded Variant
============== ===================
cqh            ecqh
pqh            epqh
psh            epsh
ssh            essh
cssh           ecssh
============== ===================

It is also possible to log all of the headers in a transaction message with a
single field.  For each original original field, there is a variant which ends in
``ah`` rather than ``h``, as shown here:

============== ===================
Original Field All Headers Variant
============== ===================
cqh            cqah
pqh            pqah
psh            psah
ssh            ssah
cssh           cssah
============== ===================

No particular header is specified when using these variants, for example::

    Format = '%<cqah>'

The output generated by these fields has the pattern::

    {{{tag1}:{value1}}{{tag2}:{value2}}...}

(The size of some messages may exceed internal buffer capacity.  This may
result in the value of the last header being truncated, in which case, the
value will end with ``...}``.  This may also result in the ommission of
entire tag/value pairs.)

.. _admin-logging-fields-methods:

HTTP Methods
~~~~~~~~~~~~

.. _cqhm:

These fields are used to log information about the HTTP methods/verbs used by
requests.

===== ============== ==========================================================
Field Source         Description
===== ============== ==========================================================
cqhm  Client Request HTTP method used in the client request to the |TS| proxy
                     (e.g. ``GET``, ``POST``, etc.).
===== ============== ==========================================================

.. _admin-logging-fields-ids:

Identifiers
~~~~~~~~~~~

.. _crid:
.. _cruuid:
.. _puuid:

Logging fields used to obtain various unique identifiers for transactions or
objects.

====== ============== =========================================================
Field  Source         Description
====== ============== =========================================================
crid   Client Request Sequence number of the current client request. Resets to
                      ``0`` on every |TS| restart.
cruuid Client Request UUID of the current client request; generated by
                      concatenating the puuid_ and crid_ field values.
puuid  Proxy Server   UUID for the currently running :program:`traffic_server`
                      process. Regenerated on every |TS| startup.
====== ============== =========================================================

.. _admin-logging-fields-lengths:

Lengths and Sizes
~~~~~~~~~~~~~~~~~

.. _cqcl:
.. _cqhl:
.. _cqql:
.. _csscl:
.. _csshl:
.. _cssql:
.. _fsiz:
.. _pqcl:
.. _pqhl:
.. _pqql:
.. _pscl:
.. _pshl:
.. _psql:
.. _sscl:
.. _sshl:
.. _ssql:

These log fields are used to obtain various lengths and sizes of transaction
components (headers, content bodies, etc.) between clients, proxies, and
origins. Unless otherwise noted, all lengths are in bytes.

===== ====================== ==================================================
Field Source                 Description
===== ====================== ==================================================
cqcl  Client Request         Client request content length, in bytes.
cqhl  Client Request         Client request header length, in bytes.
cqql  Client Request         Client request header and content length combined,
                             in bytes.
csscl Cached Origin Response Content body length from cached origin response.
csshl Cached Origin Response Header length from cached origin response.
cssql Cached Origin Response Content and header length from cached origin
                             response.
fsiz  Origin                 Size of the file as seen by the origin server.
pqcl  Proxy Request          Content body length of the |TS| proxy request to
                             the origin server.
pqhl  Proxy Request          Header length of the |TS| proxy request to the
                             origin server.
pqql  Proxy Request          Content body and header length combined, of the
                             |TS| request to the origin server.
pscl  Proxy Response         Content body length of the |TS| proxy response.
pshl  Proxy Response         Header length of the |TS| response to client.
psql  Proxy Response         Content body and header length combined of the
                             |TS| response to client.
sscl  Origin Response        Content body length of the origin server response
                             to |TS|.
sshl  Origin Response        Header length of the origin server response.
ssql  Origin Response        Content body and header length combined of the
                             origin server response to |TS|.
===== ====================== ==================================================

.. _admin-logging-fields-collation:

Log Collation
~~~~~~~~~~~~~

.. _phn:
.. _phi:

Logging fields related to :ref:`admin-logging-collation`.

===== ====== ==================================================================
Field Source Description
===== ====== ==================================================================
phn   Proxy  Hostname of the |TS| node which generated the collated log entry.
phi   Proxy  IP of the |TS| node which generated the collated log entry.
===== ====== ==================================================================

.. note::

   Log collation is a *deprecated* feature as of ATS v8.0.0, and  will be
   removed in ATS v9.0.0.

.. _admin-logging-fields-network:

Network Addresses, Ports, and Interfaces
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _chi:
.. _chih:
.. _hii:
.. _hiih:
.. _chp:
.. _php:
.. _pqsi:
.. _pqsp:
.. _shi:
.. _shn:
.. _nhi:
.. _nhp:

The following log fields are used to log details of the network (IP) addresses,
incoming/outgoing ports, and network interfaces used during transactions.

===== ============== ==========================================================
Field Source         Description
===== ============== ==========================================================
chi   Client         IP address of the client's host.
chih  Client         IP address of the client's host, in hexadecimal.
hii   Proxy          IP address for the proxy's incoming interface (to which
                     the client connected).
hiih  Proxy          IP address for the proxy's incoming interface (to which
                     the client connected), in hexadecimal.
chp   Client         Port number of the client's host.
php   Proxy Response TCP port number from which |TS| serviced the request.
pqsi  Proxy Request  IP address from which |TS| issued the proxy request to the
                     origin server. Cache hits will result in a value of ``0``.
pqsp  Proxy Request  Port number from which |TS| issued the proxy request to
                     the origin server. Cache hits will yield a value of ``0``.
shi   Origin Server  IP address resolved via DNS by |TS| for the origin server.
                     For hosts with multiple IP addresses, the address used by
                     |TS| for the connection will be reported. See note below
                     regarding misleading values from cached documents.
shn   Origin Server  Host name of the origin server.
nhi   Origin Server  Destination IP address of next hop
nhp   Origin Server  Destination port of next hop
===== ============== ==========================================================

.. note::

    This can be misleading for cached documents. For example: if the first
    request was a cache miss and came from *IP1* for server *S* and the second
    request for server *S* resolved to *IP2* but came from the cache, then the
    log entry for the second request will show *IP2*.

.. _admin-logging-fields-plugin:

Plugin Details
~~~~~~~~~~~~~~

.. _piid:
.. _pitag:
.. _cqint:

Logging fields which may be used to obtain details of plugins involved in the
transaction.

===== ================ ============================================================
Field Source           Description
===== ================ ============================================================
piid  Proxy Plugin     Plugin ID for the current transaction. This is set for
                       plugin driven transactions via
                       :c:func:`TSHttpConnectWithPluginId`.
pitag Proxy Plugin     Plugin tag for the current transaction. This is set for
                       plugin driven transactions via
                       :c:func:`TSHttpConnectWithPluginId`.
cqint Client Request   If a request was generated internally (via a plugin), then
                       this has a value of ``1``, otherwise ``0``. This can be
                       useful when tracking internal only requests, such as those
                       generated by the ``authproxy`` plugin.
===== ================ ============================================================

.. _admin-logging-fields-proto:

Protocols and Versions
~~~~~~~~~~~~~~~~~~~~~~

.. _cqhv:
.. _cqpv:
.. _csshv:
.. _sshv:

These logging fields may be used to determine which protocols and/or versions
were in effect for a given event.

===== ===================== ===================================================
Field Source                Description
===== ===================== ===================================================
cqhv  Client Request        Client request HTTP version.
cqpv  Client Request        Client request protocol and version.
csshv Cached Proxy Response Origin server's HTTP version from cached version of
                            the document in |TS| proxy cache.
sshv  Origin Response       Origin server's response HTTP version.
===== ===================== ===================================================

.. _admin-logging-fields-request:

Request Details
~~~~~~~~~~~~~~~

.. _cqtx:

The following logging fields are used to obtain the actual HTTP request
details.

===== ============== ==========================================================
Field Source         Description
===== ============== ==========================================================
cqtx  Client Request The full HTTP client request text, minus headers, e.g.
                     ``GET http://www.company.com HTTP/1.0``. In reverse proxy
                     mode, |TS| logs rewritten/mapped URL (according to the
                     rules in :file:`remap.config`), not the pristine/unmapped
                     URL.
===== ============== ==========================================================

.. _admin-logging-fields-ssl:

SSL / Encryption
~~~~~~~~~~~~~~~~

.. _cqssl:
.. _cqssr:
.. _cqssv:
.. _cqssc:
.. _pqssl:

Fields which expose the use, or lack thereof, of specific SSL and encryption
features.

===== ============== ==========================================================
Field Source         Description
===== ============== ==========================================================
cqssl Client Request SSL client request status indicates if this client
                     connection is over SSL.
cqssr Client Request SSL session ticket reused status; indicates if the current
                     request hit the SSL session ticket and avoided a full SSL
                     handshake.
cqssv Client Request SSL version used to communicate with the client.
cqssc Client Request SSL Cipher used by |TS| to communicate with the client.
pqssl Proxy Request  Indicates whether the connection from |TS| to the origin
                     was over SSL or not.
===== ============== ==========================================================

.. _admin-logging-fields-status:

Status Codes
~~~~~~~~~~~~

.. _cfsc:
.. _csssc:
.. _pfsc:
.. _pssc:
.. _sssc:
.. _prrp:

These log fields provide a variety of status codes, some numeric and some as
strings, relating to client, proxy, and origin transactions.

===== ===================== ===================================================
Field Source                Description
===== ===================== ===================================================
cfsc  Client Request        Finish status code specifying whether the client
                            request to |TS| was successfully completed
                            (``FIN``) or interrupted (``INTR``).
csssc Cached Proxy Response HTTP response status code of the origin server
                            response, as cached by |TS|.
pfsc  Proxy Request         Finish status code specifying whether the proxy
                            request from |TS| to the origin server was
                            successfully completed (``FIN``), interrupted
                            (``INTR``), or timed out (``TIMEOUT``).
prrp  Proxy Response        HTTP response reason phrase sent by |TS| proxy to the
                            client.
pssc  Proxy Response        HTTP response status code sent by |TS| proxy to the
                            client.
sssc  Origin Response       HTTP response status code sent by the origin server
                            to the |TS| proxy.
===== ===================== ===================================================

.. _admin-logging-fields-tcp:

TCP Details
~~~~~~~~~~~

.. _cqtr:
.. _cqmpt:

The following logging fields reveal information about the TCP layer of client,
proxy, and origin server connections.

===== ============== ==========================================================
Field Source         Description
===== ============== ==========================================================
cqtr  Client Request TCP reused status of the connection between the client and
                     |TS| proxy, indicating whether the request was delivered
                     through an already established connection.
cqmpt Client Request Indicates the MPTCP state of the connection. ``-1`` means
                     MPTCP was not enabled on the listening port, whereas ``0``
                     and ``1`` indicates whether MPTCP was successfully
                     negotiated or not.
===== ============== ==========================================================

.. _admin-logging-fields-time:

Timestamps and Durations
~~~~~~~~~~~~~~~~~~~~~~~~

.. _cqtd:
.. _cqtn:
.. _cqtq:
.. _cqts:
.. _cqtt:
.. _crat:
.. _ms:
.. _msdms:
.. _stms:
.. _stmsh:
.. _stmsf:
.. _sts:
.. _ttms:
.. _ttmsh:
.. _ttmsf:
.. _tts:

The logging fields expose a variety of timing related information about client,
proxy, and origin transactions. Variants of some of the fields provide timing
resolution of the same underlying detail in milliseconds and seconds (both
fractional and rounded-down integers). These variants are particularly useful
in accommodating the emulation of other HTTP proxy softwares' logging formats.

Other fields in this category provide variously formatted timestamps of
particular events within the current transaction (e.g. the time at which a
client request was received by |TS|).

===== ======================= =================================================
Field Source                  Description
===== ======================= =================================================
cqtd  Client Request          Client request timestamp. Specifies the date of
                              the client request in the format ``YYYY-MM-DD``
                              (four digit year, two digit month, two digit day
                              - with leading zeroes as necessary for the latter
                              two).
cqtn  Client Request          Client request timestamp in the Netscape
                              timestamp format.
cqtq  Client Request          The time at which the client request was received
                              expressed as fractional (floating point) seconds
                              since midnight January 1, 1970 UTC (epoch), with
                              millisecond resolution.
cqts  Client Request          Same as cqtq_, but as an integer without
                              sub-second resolution.
cqth  Client Request          Same as cqts_, but represented in hexadecimal.
cqtt  Client Request          Client request timestamp in the 24-hour format
                              ``hh:mm:ss`` (two digit hour, minutes, and
                              seconds - with leading zeroes as necessary).
crat  Origin Response         Retry-After time in seconds if specified in the
                              origin server response.
ms    Proxy                   Timestamp in milliseconds of a specific milestone
                              for this request. See note below about specifying
                              which milestone to use.
msdms Proxy                   Difference in milliseconds between the timestamps
                              of two milestones. See note below about
                              specifying which milestones to use.
stms  Proxy-Origin Connection Time (in milliseconds) spent accessing the origin
                              server. Measured from the time the connection
                              between proxy and origin is established to the
                              time it was closed.
stmsh Proxy-Origin Connection Same as stms_, but represented in hexadecimal.
stmsf Proxy-Origin Connection Same as stms_, but in fractional (floating point)
                              seconds.
sts   Proxy-Origin Connection Same as stms_, but in integer seconds (no
                              sub-second precision).
ttms  Client-Proxy Connection Time in milliseconds spent by |TS| processing the
                              entire client request. Measured from the time the
                              connection between the client and |TS| proxy was
                              established until the last byte of the proxy
                              response was delivered to the client.
ttmsh Client-Proxy Connection Same as ttms_, but represented in hexadecimal.
ttmsf Client-Proxy Connection Same as ttms_, but in fraction (floating point)
                              seconds.
tts   Client Request          Same as ttms_, but in integer seconds (no
                              sub-second precision).
===== ======================= =================================================

.. note::

    Logging fields for transaction milestones require specifying which of the
    milestones to use. Similar to how header logging fields are used, these log
    fields take the milestone name(s) in between curly braces, immediately
    before the logging field name, as so::

        %<{Milestone field name}ms>
        %<{Milestone field name1-Milestone field name2}msdms>

    For more information on transaction milestones in |TS|, refer to the
    documentation on :c:func:`TSHttpTxnMilestoneGet`.

.. _admin-logging-fields-urls:

URLs, Schemes, and Paths
~~~~~~~~~~~~~~~~~~~~~~~~

.. _cqu:
.. _cquc:
.. _cqup:
.. _cqus:
.. _cquuc:
.. _cquup:
.. _cquuh:

These log fields allow capture of URLs, or components (such as schemes and
paths), from transactions processed by |TS|.

===== ============== ==========================================================
Field Source         Description
===== ============== ==========================================================
cqu   Proxy Request  URI of the client request to |TS| (a subset of cqtx_). In
                     reverse proxy mode, |TS| logs the rewritten/mapped URL
                     (according to the rules in :file:`remap.config`), not the
                     pristine/unmapped URL.
cquc  Client Request Canonical URL from the client request to |TS|. This field
                     differs from cqu_ by having its contents URL-escaped
                     (spaces and various other characters are replaced by
                     percent-escaped entity codes).
cqup  Proxy Request  Path component from the remapped client request.
cqus  Client Request URL scheme from the client request.
cquuc Client Request Canonical (prior to remapping) effective URL from client request.
cquup Client Request Canonical (prior to remapping) path component from the
                     client request. Compare with cqup_.
cquuh Client Request Unmapped URL host from the client request.
===== ============== ==========================================================

Log Field Slicing
=================

It is sometimes desirable to slice a log field to limit the length of a given
log field's output.

Log Field slicing can be specified as below::

    %<field[start:end]>
    %<{field}container[start:end]>

Omitting the slice notation defaults to the entire log field.

Slice notation only applies to a log field that is of type string and can not
be applied to IPs or timestamp which are converted to string from integer.

The below slice specifiers are allowed.

``[start:end]``
          Log field value from start through end-1
``[start:]``
          Log field value from start through the rest of the string
``[:end]``
          Log field value from the beginning through end-1
``[:]``
          Default - entire Log field

Some examples below ::

  '%<cqup>'       // the whole characters of <cqup>.
  '%<cqup>[:]'    // the whole characters of <cqup>.
  '%<cqup[0:30]>' // the first 30 characters of <cqup>.
  '%<cqup[-10:]>' // the last 10 characters of <cqup>.
  '%<cqup[:-5]>'  // everything except the last 5 characters of <cqup>.
