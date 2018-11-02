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
.. highlight:: none

.. _admin-plugins-header-rewrite:

Header Rewrite Plugin
*********************

This plugin allows you to modify arbitrary headers based on defined rules, for
both requests and responses.

Purpose
=======

Remapping an incoming client request to an origin server is at the heart of
what we use |TS| for, but often we need to do more to requests than just change
their destination according to simple rewriting rules against the URL.

We may need to direct requests to different origins based on a client cookie.

Our origins might return error response codes which are a little too customized
and we want to condense the possible values to just the official codes.

We might want to strip a set of internal-use or debugging related HTTP headers
from responses before sending them to clients, unless the original request had
its own special header indicating that they should be retained.

Or perhaps we want to redirect certain requests differently when they come from
a known group of IP addresses (say, our developers' office network) and we have
a file on our proxy caching servers called ``/var/developertesting``. (Stranger
QA methods exist.)

Maybe we want to deny access to a resource with an HTTP 403 if the client
connected to |TS| over a particular port, used ``HEAD`` instead of ``GET``,
doesn't list Spanish (Paraguay dialect) in their ``Accept-Language`` header,
and either the origin server replied with 304 or we randomly generate an
integer between 0 and 500 and get back anything greater than 290.

These more complicated transformations of requests and responses (even that
last one) are made possible by this plugin.

Installation
============

This plugin is considered stable and is included with |TS| by default. There
are no special steps necessary for its installation.

Configuration
=============

Header rewrite configurations, the actual conditions and operations that make
up the activity performed by the plugin, are specified in external files and
not in-line with the various mapping (and remapping) rules you may have
configured for your proxy. The location of this file is arbitrary, as long as
the |TS| processes have permissions to read it, though you may find it useful
to keep it in the same location as your other proxy configuration files.

The paths given to the configuration file(s) may be absolute (leading with a
``/`` character), or they may be relative to the |TS| configuration directory.

There are two methods for enabling this plugin, based on whether you wish it to
operate globally on every request that passes through your proxy, or only on
some subset of the requests by enabling it only for specific mapping rules.

Enabling Globally
-----------------

This plugin may be enabled globally, so that the conditions and header
rewriting rules are evaluated for every request made to your |TS| instance.
This is done by adding the following line to your :file:`plugin.config`::

  header_rewrite.so config_file_1.conf config_file_2.conf ...

You may specify multiple configuration files. Their rules will be evaluated in
the order the files are listed.

Enabling Per-Mapping
--------------------

Alternatively, the plugin can be enabled for specific mapping rules, by
modifying the relevant entries in your :file:`remap.config`::

  map http://a http://b @plugin=header_rewrite.so @pparam=rules1.conf ...

As with the global method above, multiple configuration files may be listed,
each with its own ``@pparam=<file>`` and their contents will be evaluated in
the order the files were specified.

Each ruleset that is configured per-mapping should have a special
`Hook Conditions`_ defined. Without a defined hook, these rulesets will use the
``REMAP_PSEUDO_HOOK``.

Rewriting Rules
===============

Header rewriting rules consist of zero or more `Conditions`_ followed by one or
more `Operators`_. Conditions are used to limit the requests which will be
affected by the operator(s). Additionally, both conditions and operators may
have flags which modify their behavior.

A complete rule, consisting of two conditions and a single operator might look
like the following::

    cond %{STATUS} >399 [AND]
    cond %{STATUS} <500
    set-status 404

Which converts any 4xx HTTP status code from the origin server to a 404. A
response from the origin with a status of 200 would be unaffected by this rule.

Conditions
----------

Conditions are used as qualifiers, causing the associated operators to only be
evaluated if the condition(s) are met. Conditions all take the following form::

    cond %{<condition name>[:<argument>]} <operand> [<flags>]

Every condition begins with the literal string ``cond`` to indicate that this
line is a condition, not an operator. This is followed by the condition name,
inside curly braces and preceded by a percent sign (e.g. ``%{TRUE}`` for the
condition named ``TRUE``). Some condition names take an argument. Header
conditions, for example, take the name of the header in question, and cookie
conditions take the name of the cookie. For these, the condition name is
followed by a colon and the argument value (e.g. ``%{HEADER:User-Agent}`` for a
header condition against the ``User-Agent`` header).

The operand of a condition provides a value, pattern, or range against which to
match. The format is described in `Condition Operands`_ below.

Finally, a condition may optionally have various flags associated with it.
These are described in `Condition Flags`_ below.

The following sections list all of the condition types currently supported. For
increased clarity in their usage, the optional ``[<flags>]`` portion of the
condition is omitted from all of the examples.

ACCESS
~~~~~~
::

    cond %{ACCESS:<path>}

Returns true if |TS| was able to successfully update the access time on the
file at ``<path>``. This condition will return false if the file does not exist
or |TS| cannot access it for any other reason.

CLIENT-HEADER
~~~~~~~~~~~~~
::

    cond %{CLIENT-HEADER:<name>} <operand>

Value of the header ``<name>`` from the original client request (regardless of
the hook context in which the rule is being evaluated). Note that some headers
may appear in an HTTP message more than once. In these cases, the value of the
header operated on by this condition will be a comma separated string of the
values from every occurrence of the header. More details are provided in
`Repeated Headers`_ below.

CLIENT-URL
~~~~~~~~~~
::

    cond %{CLIENT-URL:<part>} <operand>

The URL of the original request. Regardless of the hook context in which the
rule is evaluated, this condition will always operate on the original, unmapped
URL supplied by the client. The ``<part>`` may be specified according to the
options documented in `URL Parts`_.

Note that the HOST ``<part>`` of the CLIENT-URL might not be set until the remap
phase of the transaction.  This happens when there is no host in the incoming URL
and only set as a host header.  During the remap phase the host header is copied
to the CLIENT-URL.  Use CLIENT-HEADER:Host if you are going to match the host.

COOKIE
~~~~~~
::

    cond %{COOKIE:<name>} <operand>

Value of of the cookie ``<name>``. This does not expose or match against a
cookie's expiration, the domain(s) on which it is valid, whether it is protocol
restricted, or any of the other metadata; simply the current value of the
cookie as presented by the client.

FROM-URL
~~~~~~~~
::

    cond %{FROM-URL:<part>} <operand>

In a remapping context, this condition matches against the source URL from
which the remapping was generated. This condition is valid only within
configurations provided through :file:`remap.config` as described in `Enabling
Per-Mapping`_ above.

The ``<part>`` allows the operand to match against just a component of the URL,
as documented in `URL Parts`_ below.

GEO
~~~
::

    cond %{GEO:<part>} <operand>

Perform a GeoIP lookup of the client-IP, using a 3rd party library and
DB. Currently only the MaxMind GeoIP API is supported. The default is to
do a Country lookup, but the following qualifiers are supported::

    %{GEO:COUNTRY}      The country code (e.g. "US")
    %{GEO:COUNTRY-ISO}  The country ISO code (e.g. 225)
    %{GEO:ASN}          The AS number for the provider network (e.g. 7922)
    %{GEO:ASN-NAME}     A descriptive string of the AS provider

These operators can be used both as conditionals, as well as values for
setting headers. For example::

    cond %{SEND_RESPONSE_HDR_HOOK} [AND]
    cond %${GEO:COUNTRY} =US
        set-header ATS-Geo-Country %{GEO:COUNTRY}
        set-header ATS-Geo-Country-ISO %{GEO:COUNTRY-ISO}
        set-header ATS-Geo-ASN %{GEO:ASN}
        set-header ATS-Geo-ASN-NAME %{GEO:ASN-NAME}


HEADER
~~~~~~
::

    cond %{HEADER:<name>} <operand>

Value of the header ``<name>`` from either the original client request or the
origin server's response, depending upon the hook context in which the rule is
being evaluated. Consult `Requests vs. Responses`_ for more information on how
to distinguish the two, as well as enforce that a rule is always evaluated in
the desired context.

Note that some headers may appear in an HTTP message more than once. In these
cases, the value of the header operated on by this condition will be a comma
separated string of the values from every occurrence of the header. Refer to
`Repeated Headers`_ for more information.

If you wish to use a client request header, regardless of hook context, you may
consider using the `CLIENT-HEADER`_ condition instead.

ID
~~
::

   cond %{ID:REQUEST} >100

This condition provides access to three identifier values that ATS uses
internally for things like logging and debugging. Since these are IDs, they
are mostly useful as a value (operand) to other operators. The three types of
IDs are ::

    %{ID:REQUEST}    A unique, sequence number for the transaction
    %{ID:PROCESS}    A UUID string, generated every time ATS restarts
    %{ID:UNIQUE}     The combination of the previous two IDs

Now, even though these are conditionals, their primary use are as value
arguments to another operator. For example::

    set-header ATS-Req-UUID %{ID:UNIQUE}

CIDR
~~~~
::

   set-header @Client-CIDR %{CIDR:24,48}

This condition takes the client IP, and applies the provided CIDR style masks
to the IP, before producing a string. The typical use of this conditions is as
above, producing a header that contains a IP representation which has some
privacy properties. It can of course also be used as a regular condition, and
the output is a string that can be compared against. The two optional
arguments are as follows::

    IPv4-Mask    Length, in bits, of the IPv4 address to preserve. Default: 24
    IPv6-Mask    Length, in bits, of the IPv6 address to preserve. Default: 48

The two arguments, if provided, are comma separated. Valid syntax includes::

    %{CIDR}         Defaults to 24,48 (as above)
    %{CIDR:16}      IPv4 CIDR mask is 16 bits, IPv6 mask is 48
    %{CIDR:18,42}   IPv4 CIDR mask is 18 bits, IPv6 mask is 42 bits

A typical use case is to insert the @-prefixed header as above, and then use
this header in a custom log format, rather than logging the full client
IP. Another use case could be to make a special condition on a sub-net,
e.g.::

    cond %{CIDR:8} ="8.0.0.0"
        set-header X-Is-Eight "Yes"

This condition has no requirements other than access to the Client IP, hence,
it should work in any and all hooks.

INBOUND
~~~~~~~
::

   cond %{INBOUND:TLS} /./

This condition provides access to information about the inbound (client, user agent) connection to ATS.
The data that can be checked is ::

   %{INBOUND:LOCAL-ADDR}      The local (ATS) address for the connection. Equivalent to %{IP:INBOUND}.
   %{INBOUND:LOCAL-PORT}      The local (ATS) port for the connection. Equivalent to %{INCOMING-PORT}.
   %{INBOUND:REMOTE-ADDR}     The client address for the connection. Equivalent to %{IP:CLIENT}.
   %{INBOUND:REMOTE-PORT}     The client port for the connection.
   %{INBOUND:TLS}             The TLS protocol if the connection is over TLS, otherwise the empty string.
   %{INBOUND:H2}              The string "h2" if the connection is HTTP/2, otherwise the empty string.
   %{INBOUND:IPV4}            The string "ipv4" if the connection is IPv4, otherwise the emtpy string.
   %{INBOUND:IPV6}            The string "ipv6" if the connection is IPv6, otherwise the empty string.
   %{INBOUND:IP-FAMILY}       The IP family, either "ipv4" or "ipv6".
   %{INBOUND:STACK}           The full protocol stack separated by ','.

All of the tags generated by this condition are from the :ref:`protocol stack tags <protocol_tags>`.

Because of the implicit match rules, using these as conditions is a bit unexpected. The condition
listed above will be true if the inbound connection is over TLS because ``%{INBOUND:TLS}`` will only
return a non-empty string for TLS connections. In contrast ::

   cond %{INBOUND:TLS}

will be true for *non*-TLS connections because it will be true when ``%{INBOUND:TLS}`` is the empty
string. This happens because the default matching is equality and the default value the empty
string. Therefore the condition is treated as if it were ::

  cond %{INBOUND:TLS}=""

which is true when the connection is not TLS. The arguments ``H2``, ``IPV4``, and ``IPV6`` work the
same way.


INCOMING-PORT
~~~~~~~~~~~~~
::

    cond %{INCOMING-PORT} <operand>

TCP port, as a decimal integer, on which the incoming client connection was
made.

This condition is *deprecated* as of ATS v8.0.x, please use ``%{INBOUND:LOCAL-PORT}`` instead.

IP
~~
::

    cond %{IP:<part>} <operand>

This is one of four possible IPs associated with the transaction, with the
possible parts being
::

    %{IP:CLIENT}     Client's IP address. Equivalent to %{INBOUND:REMOTE-ADDR}.
    %{IP:INBOUND}    ATS's IP address to which the client connected. Equivalent to %{INBOUND:LOCAL-ADDR}
    %{IP:SERVER}     Upstream (next-hop) server IP address (typically origin, or parent)
    %{IP:OUTBOUND}   ATS's outbound IP address, that was used to connect upstream (next-hop)

Note that both `%{IP:SERVER}` and `%{IP:OUTBOUND}` can be unset, in which case the
empty string is returned. The common use for this condition is
actually as a value to an operator, e.g. ::

   cond %{SEND_RESPONSE_HDR_HOOK}
     set-header X-Client-IP %{IP:CLIENT}
     set-header X-Inbound-IP %{IP:INBOUND}
     set-header X-Server-IP %{IP:SERVER}
     set-header X-Outbound-IP %{IP:OUTBOUND}

Finally, this new condition replaces the old %{CLIENT-IP} condition, which is
now properly deprecated. It will be removed as of ATS v8.0.0.

INTERNAL-TRANSACTION
~~~~~~~~~~~~~~~~~~~~
::

    cond %{INTERNAL-TRANSACTION}

Returns true if the current transaction was internally-generated by |TS| (using
:c:func:`TSHttpTxnIsInternal`). These transactions are not initiated by
external client requests, but are triggered (often by plugins) entirely within
the |TS| process.

METHOD
~~~~~~~
::

    cond %{METHOD} <operand>

The HTTP method (e.g. ``GET``, ``HEAD``, ``POST``, and so on) used by the
client for this transaction.

NOW
~~~
::

    cond %{NOW:<part>} <operand>

This is the current time, in the local timezone as set on the machine,
typically GMC. Without any further qualifiers, this is the time in seconds
since EPOCH aka Unix time. Qualifiers can be used to give various other
values, such as year, month etc.
::

    %{NOW:YEAR}      Current year (e.g. 2016)
    %{NOW:MONTH}     Current month (0-11, 0 == January)
    %{NOW:DAY}       Current day of the month (1-31)
    %{NOW:HOUR}      Current hour (0-23, in the 24h system)
    %{NOW:MIN}       Current minute (0-59}
    %{NOW:WEEKDAY}   Current weekday (0-6, 0 == Sunday)
    %{NOW:YEARDAY}   Current day of the year (0-365, 0 == Jan 1st)

PATH
~~~~
::

    cond %{PATH} <operand>

The path component of the transaction. This does NOT include the leading ``/`` that
immediately follows the hostname and terminates prior to the ``?`` signifying
the beginning of query parameters (or the end of the URL, whichever occurs
first).

Refer to `Requests vs. Responses`_ for more information on determining the
context in which the transaction's URL is evaluated.

This condition is *deprecated* as of ATS v7.1.x, please use e.g. %{URL:PATH}
or %{CLIENT-URL:PATH} instead.


QUERY
~~~~~
::

    cond %{QUERY} <operand>

The query parameters, if any, of the transaction.  Refer to `Requests vs.
Responses`_ for more information on determining the context in which the
transaction's URL is evaluated.

This condition is *deprecated* as of ATS v7.1.x, please use e.g. %{URL:QUERY}
or %{CLIENT-URL:QUERY} instead.


RANDOM
~~~~~~
::

    cond %{RANDOM:<n>} <operand>

Generates a random integer between ``0`` and ``<n>``, inclusive.

STATUS
~~~~~~
::

    cond %{STATUS} <operand>

Numeric HTTP status code of the response.

TO-URL
~~~~~~
::

    cond %{TO-URL:<part>} <operand>

In a remapping context, this condition matches against the target URL to which
the remapping is directed. This condition is valid only within configurations
provided through :file:`remap.config` as described in `Enabling Per-Mapping`_
above.

The ``<part>`` allows the operand to match against just a component of the URL,
as documented in `URL Parts`_ below.

TRUE / FALSE
~~~~~~~~~~~~
::

    cond %{TRUE}
    cond %{FALSE}

These conditions always return a true value and a false value, respectively.
The true condition is implicit in any rules which specify no conditions (only
operators).

TXN-COUNT
~~~~~~~~~
::

    cond %{TXN-COUNT} <operand>

Returns the number of transactions (including the current one) that have been sent on the current
client connection. This can be used to detect if the current transaction is the first transaction.

URL
~~~
::

    cond %{URL:<part>} <operand>

The complete URL of the current transaction. This will automatically choose the
most relevant URL depending upon the hook context in which the condition is
being evaluated.

Refer to `Requests vs. Responses`_ for more information on determining the
context in which the transaction's URL is evaluated.  The ``<part>`` may be
specified according to the options documented in `URL Parts`_.

Condition Operands
------------------

Operands provide the means to restrict the values, provided by a condition,
which will lead to that condition evaluating as true. There are currently four
types supported:

=========== ===================================================================
Operand     Description
=========== ===================================================================
/regex/     Matches the condition's provided value against the regular
            expression.
<string     Matches if the value from the condition is lexically less than
            *string*.
>string     Matches if the value from the condition is lexically greater than
            *string*.
=string     Matches if the value from the condition is lexically equal to
            *string*.
=========== ===================================================================

The absence of an operand for conditions which accept them simply requires that
a value exists (e.g. the content of the header is not an empty string) for the
condition to be considered true.

Condition Flags
---------------

The condition flags are optional, and you can combine more than one into
a comma-separated list of flags. Note that whitespaces are not allowed inside
the brackets:

====== ========================================================================
Flag   Description
====== ========================================================================
AND    Indicates that both the current condition and the next must be true.
       This is the default behavior for all conditions when no flags are
       provided.
NOT    Inverts the condition.
OR     Indicates that either the current condition or the next one must be
       true, as contrasted with the default behavior from ``[AND]``.
====== ========================================================================

Operators
---------

Operators are the part of your header rewriting rules which actually modify the
header content of your requests and responses. They are always the final part
of a rule, following any of the conditions which whittled down the requests and
responses to which they will be applied.

Multiple operators may be specified for a single rule, and they will be
executed in the order listed. The end of the rule is marked either by the end
of the configuration file or the next appearance of a condition (whichever
occurs first).

The following operators are available:

add-cookie
~~~~~~~~~~
::

  add-cookie <name> <value>

Adds a new ``<name>`` cookie line with the contents ``<value>``. Note that this
operator will do nothing if a cookie pair with ``<name>`` already exists.

add-header
~~~~~~~~~~
::

  add-header <name> <value>

Adds a new ``<name>`` header line with the contents ``<value>``. Note that this
operator can produce duplicate headers if one of ``<name>`` already exists, or
your configuration supplies multiple instances of this operator in different
rules which are all invoked. This is not an issue for headers which may safely
be specified multiple times, such as ``Set-Cookie``, but for headers which may
only be specified once you may prefer to use `set-header`_ instead.

The header's ``<value>`` may be specified as a literal string, or it may take
advantage of :ref:`header-rewrite-expansion` to calculate a dynamic value for the header.

counter
~~~~~~~
::

  counter <name>

Increments an integer counter called ``<name>`` every time the rule is invoked.
The counter is initialized at ``0`` if it does not already exist. The name you
give your counter is arbitrary, though it is strongly advisable to avoid
conflicts with existing |TS| statistics.

This counter can be viewed at any time through the standard statistics APIs,
including the :ref:`Stats Over HTTP plugin <admin-plugins-stats-over-http>`.

Counters can only increment by 1 each time this operator is invoked. There is
no facility to increment by other amounts, nor is it possible to initialize the
counter with any value other than ``0``. Additionally, the counter will reset
whenever |TS| is restarted.

no-op
~~~~~
::

  no-op

This operator does nothing, takes no arguments, and has no side effects.

rm-header
~~~~~~~~~
::

  rm-header <name>

Removes the header ``<name>``.

rm-cookie
~~~~~~~~~
::

  rm-cookie <name>

Removes the cookie ``<name>``.

set-config
~~~~~~~~~~
::

  set-config <name> <value>

Allows you to override the value of a |TS| configuration variable for the
current connection. The variables specified by ``<name>`` must be overridable.
For details on available |TS| configuration variables, consult the
documentation for :file:`records.config`. You can read more about overridable
configuration variables in the developer's documentation for
:ref:`ts-overridable-config`.

set-conn-dscp
~~~~~~~~~~~~~
::

  set-conn-dscp <value>

When invoked, sets the client side `DSCP
<https://en.wikipedia.org/wiki/Differentiated_services>`_ value for the current
transaction.  The ``<value>`` should be specified as a decimal integer.

set-conn-mark
~~~~~~~~~~~~~
::

  set-conn-mark <value>

When invoked, sets the client side MARK value for the current
transaction.  The ``<value>`` should be specified as a decimal integer.
Requires at least Linux 2.6.25.

set-debug
~~~~~~~~~
::

  set-debug

When invoked, this operator enables the internal transaction debugging flag
(via :c:func:`TSHttpTxnDebugSet`), which causes debug messages to be printed to
the appropriate logs even when the debug tag has not been enabled. For
additional information on |TS| debugging statements, refer to
:ref:`developer-debug-tags` in the developer's documentation.

set-destination
~~~~~~~~~~~~~~~
::

  set-destination <part> <value>

Modifies individual components of the remapped destination's address. When
changing the remapped destination, ``<part>`` should be used to indicate the
component that is being modified (see `URL Parts`_), and ``<value>`` will be
used as its replacement. You must supply a non-zero length value, otherwise
this operator will be an effective no-op (though a warning will be emitted to
the logs if debugging is enabled).

set-header
~~~~~~~~~~
::

  set-header <name> <value>

Replaces the value of header ``<name>`` with ``<value>``, creating the header
if necessary.

The header's ``<value>`` may be specified according to `Header Values`_ or take
advantage of :ref:`header-rewrite-expansion` to calculate a dynamic value for the header.

set-redirect
~~~~~~~~~~~~
::

  set-redirect <code> <destination>

When invoked, sends a redirect response to the client, with HTTP status
``<code>``, and a new location of ``<destination>``. If the ``QSA`` flag is
enabled, the original query string will be preserved and added to the new
location automatically. This operator supports :ref:`header-rewrite-expansion` for
``<destination>``.

set-status
~~~~~~~~~~
::

  set-status <code>

Modifies the :ref:`HTTP status code <appendix-http-status-codes>` used for the
response. ``<code>`` must be a valid status code. This operator will also
update the reason in the HTTP response, based on the code you have chosen. If
you wish to override that with your own text, you will need to invoke the
`set-status-reason`_ operator after this one.

set-status-reason
~~~~~~~~~~~~~~~~~
::

  set-status-reason <reason>

Modifies the HTTP response to use ``<reason>`` as the HTTP status reason,
instead of the standard string (which depends on the :ref:`HTTP status code
<appendix-http-status-codes>` used).

set-timeout-out
~~~~~~~~~~~~~~~
::

  set-timeout-out <type> <value>

Modifies the timeout values for the current transaction to ``<value>``
(specified in milliseconds). The ``<type>`` must be one of the following:
``active``, ``inactive``, ``connect``, or ``dns``.

skip-remap
~~~~~~~~~~
::

  skip-remap <value>

When invoked, and when ``<value>`` is any of ``1``, ``true``, or ``TRUE``, this
operator causes |TS| to abort further request remapping. Any other value and
the operator will effectively be a no-op.

set-cookie
~~~~~~~~~~
::

  set-cookie <name> <value>

Replaces the value of cookie ``<name>`` with ``<value>``, creating the cookie
if necessary.

Operator Flags
--------------

Operator flags are optional, are separated by commas when using more than one
for a single operator, and must not contain whitespace inside the brackets. For
example, an operator with the ``L`` flag would be written in this manner::

    set-destination HOST foo.bar.com [L]

The flags currently supported are:

====== ========================================================================
Flag   Description
====== ========================================================================
L      Last rule, do not continue.
QSA    Append the results of the rule to the query string.
====== ========================================================================

.. _header-rewrite-expansion:

Values and Variable Expansion
-----------------------------

.. note::

   This feature is replaced with a new string concatenations as of ATS v8.1.0. In v9.0.0  the special
   %<> string expansions below are no longer available, instead use the following mapping:

======================= ==================================================================================
Variable                New condition variable to use
======================= ==================================================================================
%<proto>                %{CLIENT-URL:SCHEME}
%<port>                 %{CLIENT-URL:PORT}
%<chi>                  %{IP:CLIENT}, %{INBOUND:REMOTE-ADDR} or e.g. %{CIDR:24,48}
%<cqhl>                 %{CLIENT-HEADER:Content-Length}
%<cqhm>                 %{METHOD}
%<cque>                 %[CLIENT-URL}
%<cquup>                %{CLIENT-URL:PATH}
======================= ==================================================================================

The %<INBOUND:...> tags can now be replaced with the %{INBOUND:...} equivalent.

You can concatenate values using strings, condition values and variable expansions via the ``+`` operator.
Variables (eg %<tag>) in the concatenation must be enclosed in double quotes ``"``::

    add-header CustomHeader "Hello from " + %{IP:SERVER} + ":" + "%<INBOUND:LOCAL-PORT>"

However, the above example is somewhat contrived to show the old tags, it should instead be written as

    add-header CustomHeader "Hello from " + %{IP:SERVER} + ":" + %{INBOUND:LOCAL-PORT}


Concatenation is not supported in condition testing.

Supported substitutions are currently the following table, however they are deprecated and you should
instead use the equivalent %{} conditions as shown above:

======================= ==================================================================================
Variable                Description
======================= ==================================================================================
%<proto>                (Deprecated) Protocol
%<port>                 (Deprecated) Port
%<chi>                  (Deprecated) Client IP
%<cqhl>                 (Deprecated) Client request length
%<cqhm>                 (Deprecated) Client HTTP method
%<cque>                 (Deprecated) Client effective URI
%<cquup>                (Deprecated) Client unmapped URI path
%<INBOUND:LOCAL-ADDR>   (Deprecated) The local (ATS) address for the inbound connection.
%<INBOUND:LOCAL-PORT>   (Deprecated) The local (ATS) port for the inbound connection.
%<INBOUND:REMOTE-ADDR>  (Deprecated) The client address for the inbound connectoin.
%<INBOUND:REMOTE-PORT>  (Deprecated) The client port for the inbound connectoin.
%<INBOUND:TLS>          (Deprecated) The TLS protocol for the inbound connection if it is over TLS, otherwise the
                        empty string.
%<INBOUND:H2>           (Deprecated) The string "h2" if the inbound connection is HTTP/2, otherwise the empty string.
%<INBOUND:IPV4>         (Deprecated) The string "ipv4" if the inbound connection is IPv4, otherwise the emtpy string.
%<INBOUND:IPV6>         (Deprecated) The string "ipv6" if the inbound connection is IPv6, otherwise the empty string.
%<INBOUND:IP-FAMILY>    (Deprecated) The IP family of the inbound connection (either "ipv4" or "ipv6").
%<INBOUND:STACK>        (Deprecated) The full protocol stack of the inbound connection separated by ','.
======================= ==================================================================================

Header Values
-------------

Setting a header with a value can take the following formats:

- Any `condition <Conditions>`_ which extracts a value from the request.

- ``$N``, where 0 <= N <= 9, from matching groups in a regular expression.

- A string (which can contain the numbered matches from a regular expression as
  described above).

- Null.

Supplying no value for a header for certain operators can lead to an effective
no-op. In particular, `add-header`_ and `set-header`_ will simply short-circuit
if no value has been supplied for the named header.

URL Parts
---------

Some of the conditions and operators which use a request or response URL as
their target allow for matching against specific components of the URL. For
example, the `CLIENT-URL`_ condition can be used to test just against the query
parameters by writing it as::

    cond %{CLIENT-URL:QUERY} <operand>

The URL part names which may be used for these conditions and actions are:

======== ======================================================================
Part     Description
======== ======================================================================
HOST     Full hostname.
PATH     URL substring beginning with (but not including) the first ``/`` after the hostname up to,
         but not including, the query string.
PORT     Port number.
QUERY    URL substring from the ``?``, signifying the beginning of the query
         parameters, until the end of the URL. Empty string if there were no
         quuery parameters.
SCHEME   URL scheme in use (e.g. ``http`` and ``https``).
URL      The complete URL.
======== ======================================================================

As another example, a remap rule might use the `set-destination`_ operator to
change just the hostname via::

  cond %{HEADER:X-Mobile} = "foo"
  set-destination HOST foo.mobile.bar.com [L]

Requests vs. Responses
======================

Both HTTP requests and responses have headers, a good number of them with the
same names. When writing a rule against, for example, the ``Connection`` or
``Via`` headers which are both valid in a request and a response, how can you
tell which is which, and how do you indicate to |TS| that the condition is
specifically and exclusively for a request header or just for a response
header? And how do you ensure that a header rewrite occurs against a request
before it is proxied?

Hook Conditions
---------------

In addition to the conditions already described above, there are a set of
special hook conditions. Only one of these conditions may be specified per
ruleset, and they must be the first condition listed. Which hook condition is
used determines the context in which the ruleset is evaluated, and whether the
other conditions will default to operating on the client request headers or the
origin response (or cache response) headers.

Hook conditions are written just like the other conditions, except that none of
them take any operands::

    cond %{<name>}

Because hook conditions must be the first condition in a ruleset, the use of
one forces the beginning of a new ruleset.

.. graphviz::
   :alt: header rewrite hooks

   digraph header_rewrite_hooks {
     graph [rankdir = LR];
     node[shape=record];

     Client[height=4, label="{ Client|{<p1>|<p2>} }"];
     ATS[height=4, fontsize=10,label="{ {{<clientside0>Global:\nREAD_REQUEST_PRE_REMAP_HOOK|<clientside01>Global:\nREAD_REQUEST_HDR_HOOK\nRemap rule:\nREMAP_PSEUDO_HOOK}|<clientside1>SEND_RESPONSE_HDR_HOOK}|ATS |{<originside0>SEND_REQUEST_HDR_HOOK|<originside1>READ_RESPONSE_HDR_HOOK} }",xlabel="ATS"];
     Origin[height=4, label="{ {<request>|<response>}|Origin }"];

     Client:p1 -> ATS:clientside0 [ label = "Request" ];
     ATS:originside0 -> Origin:request [ label="proxied request" ];

     Origin:response -> ATS:originside1 [ label="origin response" ];
     ATS:clientside1 -> Client:p2 [ label = "Response" ];
   }

READ_REQUEST_HDR_HOOK
~~~~~~~~~~~~~~~~~~~~~

Evaluates as soon as the client request has been read, but prior to any further
processing (including contacting origin servers or fetching objects from cache).
Conditions and operators which adapt to matching or manipulating request or
response entities (e.g. headers) depending on their context will all operate on
the request variants when using this hook, as there is no response data yet.

This hook is not available to remap rules.

READ_REQUEST_PRE_REMAP_HOOK
~~~~~~~~~~~~~~~~~~~~~~~~~~~

For ruleset configurations provided via :file:`remap.config`, this forces their
evaluation as soon as the request has been read, but prior to the remapping.
For all context-adapting conditions and operators, matching will occur against
the request, as there is no response data available yet.

This hook is not available to remap rules.

REMAP_PSEUDO_HOOK
~~~~~~~~~~~~~~~~~

Similar to `READ_REQUEST_HDR_HOOK`_, but only available when used in a remap
context, evaluates prior to `SEND_REQUEST_HDR_HOOK`_ and allows the rewrite to
evaluate as part of the remapping.

Because this hook is valid only within a remapping context, for configuration
files shared by both the global :file:`plugin.config` and individual remapping
entries in :file:`remap.config`, this hook condition will force the subsequent
ruleset(s) to be valid only for remapped transactions.

SEND_REQUEST_HDR_HOOK
~~~~~~~~~~~~~~~~~~~~~

Forces evaluation of the ruleset just prior to contacting origin servers (or
fetching the object from cache), but after any remapping may have occurred.

READ_RESPONSE_HDR_HOOK
~~~~~~~~~~~~~~~~~~~~~~

Rulesets evaluated within this context will process only once the origin server
response (or cached response) has been read, but prior to |TS| sending that
response to the client.

This is the default hook condition for all globally-configured rulesets.

SEND_RESPONSE_HDR_HOOK
~~~~~~~~~~~~~~~~~~~~~~

Evaluates rulesets just prior to sending the client response, but after any
cache updates may have been performed. This hook context provides a means to
modify aspects of the response sent to a client, while still caching the
original versions of those attributes delivered by the origin server.

Affected Conditions
-------------------

The following conditions are affected by the hook context in which they are
evaluated and will adjust using request or response entities automatically:

- `HEADER`_

- `METHOD`_

- `PATH`_

- `QUERY`_

- `URL`_

Affected Operators
------------------

The following operators are affected by the hook context in which they are
evaluated and will adjust modifying request or response entities automatically:

- `add-header`_

- `rm-header`_

- `set-header`_

Caveats
=======

Repeated Headers
----------------

Some headers may appear more than once in a request or a response. When this
occurs, all values will be collapsed into a single comma-delimited string
before the conditions see them. This avoids the problem of determining which
header instance out of several a condition's rule will be applied to, but it
may introduce unexpected behavior in your operands.

For example, let us assume an origin response includes a header named ``X-Foo``
which specifies a keyword of some sort. This header may appear zero or more
times and we wish to construct a `HEADER`_ condition that can handle this.

Condition A
~~~~~~~~~~~

This condition will match using a direct equality operand::

   cond %{HEADER:X-Foo} =bar

Condition B
~~~~~~~~~~~

This condition will match using an unanchored regular expression::

   cond %{HEADER:X-Foo} /bar/

Sample Headers
~~~~~~~~~~~~~~

Both conditions A and B will match this response::

   HTTP/1.1 200 OK
   Date: Mon, 08 Feb 2016 18:11:30 GMT
   Content-Length: 1234
   Content-Type: text/html
   X-Foo: bar

Only condition B will match this response::

   HTTP/1.1 200 OK
   Date: Mon, 08 Feb 2016 18:11:30 GMT
   Content-Length: 1234
   Content-Type: text/html
   X-Foo: bar
   X-Foo: baz

That is because the `HEADER`_ condition will see the value of ``X-Foo`` as
``bar,baz``. Condition B will still match this because the regular expression,
being unanchored, finds the substring ``bar``. But condition A fails, as it is
expecting the value to be the exact string ``bar``, nothing more and nothing
less.

Examples
========

Remove Origin Authentication Headers
------------------------------------

The following ruleset removes any authentication headers from the origin
response before caching it or returning it to the client. This is accomplished
by setting the hook context and then removing the cookie and basic
authentication headers.::

   cond %{READ_RESPONSE_HDR_HOOK}
   rm-header Set-Cookie
   rm-header WWW-Authenticate

Count Teapots
-------------

Maintains a counter statistic, which is incremented every time an origin server
has decided to be funny by returning HTTP 418::

   cond %{STATUS} =418
   counter plugin.header_rewrite.teapots

Normalize Statuses
------------------

For client-facing purposes only (because we set the hook context to just prior
to delivering the response back to the client, but after all processing and
possible cache updates have occurred), replaces all 4xx HTTP status codes from
the origin server with ``404``::

   cond %{SEND_RESPONSE_HDR_HOOK}
   cond %{STATUS} >399
   cond %{STATUS} <500
   set-status 404

Remove Cache Control to Origins
-------------------------------

Removes cache control related headers from requests before sending them to an
origin server::

   cond %{SEND_REQUEST_HDR_HOOK}
   rm-header Cache-Control
   rm-header Pragma

Enable Debugging Per-Request
----------------------------

Turns on |TS| debugging statements for a transaction, but only when a special
header is present in the client request::

   cond %{READ_REQUEST_HDR_HOOK}
   cond %{CLIENT-HEADER:X-Debug} =supersekret
   set-debug

Remove Internal Headers
-----------------------

Removes special internal/development headers from origin responses, unless the
client request included a special debug header::

   cond %{CLIENT-HEADER:X-Debug} =keep [NOT]
   rm-header X-Debug-Foo
   rm-header X-Debug-Bar

Return Original Method in Response Header
-----------------------------------------

This rule copies the original HTTP method that was used by the client into a
custom response header::

   cond %{SEND_RESPONSE_HDR_HOOK}
   set-header X-Original-Method %{METHOD}

Useless Example From Purpose
----------------------------

Even that useless example from `Purpose`_ in the beginning of this document is
possible to accomplish::

   cond %{INBOUND:LOCAL-PORT} =8090
   cond %{METHOD} =HEAD
   cond %{CLIENT-HEADER:Accept-Language} /es-py/ [NOT]
   cond %{STATUS} =304 [OR]
   cond %{RANDOM:500} >290
   set-status 403

Add Cache Control Headers Based on Origin Path
----------------------------------------------

This rule adds cache control headers to CDN responses based matching the origin
path.  One provides a max age and the other provides a “no-cache” statement to
two different file paths. ::

   cond %{SEND_RESPONSE_HDR_HOOK}
   cond %{CLIENT-URL:PATH} /examplepath1/
   add-header Cache-Control "max-age=3600" [L]
   cond %{SEND_RESPONSE_HDR_HOOK}
   cond %{CLIENT-URL:PATH} /examplepath2\/examplepath3\/.*/
   add-header Cache-Control "no-cache" [L]

Redirect when the Origin Server Times Out
-----------------------------------------

This rule sends a 302 redirect to the client with the requested URI's Path and
Query string when the Origin server times out or the connection is refused::

   cond %{SEND_RESPONSE_HDR_HOOK}
   cond %{STATUS} =502 [OR]
   cond %{STATUS} =504
   set-redirect 302 http://different_origin.example.com/%{CLIENT-URL:PATH} [QSA]

Check for existence of a header
-------------------------------

This rule will modify the ``Cache-Control`` header, but only if it is not
already set to some value, and the status code is a 2xx::

   cond %{READ_RESPONSE_HDR_HOOK} [AND]
   cond %{HEADER:Cache-Control} ="" [AND]
   cond %{STATUS} >199 [AND]
   cond %{STATUS} <300
   set-header Cache-Control "max-age=600, public"

Add HSTS
--------

Add the HTTP Strict Transport Security (HSTS) header if it does not exist and the inbound connection is TLS::

   cond %{READ_RESPONSE_HDR_HOOK} [AND]
   cond %{HEADER:Strict-Transport-Security} ="" [AND]
   cond %{INBOUND:TLS} /./
   set-header Strict-Transport-Security "max-age=63072000; includeSubDomains; preload"

This is mostly used by being attached to a remap rule that maps to a host known to support TLS. If
the parallel `OUTBOUND` supported is added then this could be done by checking for inbound TLS both
outbound TLS in the `SEND_REQUEST_HDR_HOOK`. However this technique may be used for a non-TLS
upstream if the goal is to require the user agent to connect to |TS| over TLS.

Close Connections for draining
------------------------------

When a healthcheck file is missing (in this example, ``/path/to/the/healthcheck/file.txt``),
add a ``Connection: close`` header to have clients drop their connection,
allowing the server to drain. Although Connection header is only available on
HTTP/1.1 in terms of protocols, but this also works for HTTP/2 connections
because the header triggers HTTP/2 graceful shutdown. This should be a global
configuration.::

   cond %{SEND_RESPONSE_HDR_HOOK}
   cond %{ACCESS:/path/to/the/healthcheck/file.txt}    [NOT,OR]
   set-header Connection "close"

Use Internal header to pass data
--------------------------------

In |TS|, a header that begins with ``@`` does not leave |TS|. Thus, you can use
this to pass data to different |TS| systems. For instance, a series of remap rules
could each be tagged with a consistent name to make finding logs easier.::

   cond %{REMAP_PSEUDO_HOOK}
   set-header @PropertyName "someproperty"

(Then in :file:`logging.yaml`, log ``%<{@PropertyName}cqh>``)
