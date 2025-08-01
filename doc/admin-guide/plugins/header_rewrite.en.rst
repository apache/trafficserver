.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs
.. default-domain:: cpp
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

The plugin takes an optional switches.

====================================== ==================================================================================================
 Option                                Description
====================================== ==================================================================================================
 ``--geo-db-path <path_to_geoip_db>``  A file path for MaxMindDB.
 ``--timezone <value>``                Timezone to use on header rewrite rules.
 ``--inbound-ip-source <value>``       The source of IP address for the client.
====================================== ==================================================================================================

Please note that these optional switches needs to appear before config files like you would do on UNIX command lines.

``--geo-db-path``
~~~~~~~~~~~~~~~~~

If MaxMindDB support has been compiled in, use this switch to point at your .mmdb file.
This also applies to the remap context.

``--timezone``
~~~~~~~~~~~~~~

This applies ``set-plugin-cntl TIMEZONE <value>`` to every transaction unconditionally.
See set-plugin-cntl for the setting values and the effect.

``--inbound-ip-source``
~~~~~~~~~~~~~~~~~~~~~~~
This applies ``set-plugin-cntl INBOUND_IP_SOURCE <value>`` to every transaction unconditionally.
See set-plugin-cntl for the setting values and the effect.

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
affected by the operator(s) and define when the ruleset is run during the
transaction (through the `Hook Conditions`_). Additionally, both conditions
and operators may have flags which modify their behavior.

A complete rule, consisting of two conditions and a single operator might look
like the following::

    cond %{READ_RESPONSE_HDR_HOOK} [AND]
    cond %{STATUS} >399 [AND]
    cond %{STATUS} <500
      set-status 404

Which converts any 4xx HTTP status code from the origin server to a 404. A
response from the origin with a status of 200 would be unaffected by this rule.

An optional ``else`` clause may be specified, which will be executed if the
conditions are not met. The ``else`` clause is specified by starting a new line
with the word ``else``. The following example illustrates this::

    cond %{STATUS} >399 [AND]
    cond %{STATUS} <500
      set-status 404
    else
      set-status 503

The ``else`` clause is not a condition, and does not take any flags, it is
of course optional, but when specified must be followed by at least one operator.

You can also do an ``elif`` (else if) clause, which is specified by starting a new line
with the word ``elif``. The following example illustrates this::

    cond %{STATUS} >399 [AND]
    cond %{STATUS} <500
      set-status 404
    elif
      cond %{STATUS} =503
        set-status 502
    else
      set-status 503

Keep in mind that nesting the ``else`` and ``elif`` clauses is not allowed, but any
number of ``elif`` clauses can be specified. We can consider these clauses are more
powerful and flexible ``switch`` statement. In an ``if-elif-else`` rule, only one
will evaluate its operators.

Similarly, each ``else`` and ``elif`` have the same implied
:ref:`Hook Condition <hook_conditions>` as the initial condition.

State variables
---------------

A set of state variables are also available for both conditions and operators.
There are currently 16 flag states, 4 8-bit integers and one 16-bit integer states.
These states are all transactional, meaning they are usable and persistent across
all hooks.

The flag states are numbers 0-15, the 8-bit integer states are numbered 0-3, and the
one 16-bit integer state is number 0.

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

CACHE
~~~~~
::

    cond %{CACHE} <operand>

This condition provides the server's cache lookup results to inform headers
where the requested data was generated from. The cache values are:

  ==========  ===========
  Value       Description
  ==========  ===========
  none        No cache lookup was attempted.
  miss        The object was not found in the cache.
  hit-stale   The object was found in the cache, but it was stale.
  hit-fresh   The object was fresh in the cache.
  skipped     The cache lookup was skipped.
  ==========  ===========

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
e.g. ::

    cond %{CIDR:8} ="8.0.0.0"
        set-header X-Is-Eight "Yes"
    cond %{CIDR:,8} ="fd00::" #note the IPv6 Mask is in the second position
        set-header IPv6Internal "true"

This condition has no requirements other than access to the Client IP, hence,
it should work in any and all hooks.

COOKIE
~~~~~~
::

    cond %{COOKIE:<name>} <operand>

Value of the cookie ``<name>``. This does not expose or match against a
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

.. _admin-plugins-header-rewrite-geo:

GEO
~~~
::

    cond %{GEO:<part>} <operand>

Perform a GeoIP lookup of the client-IP, using a 3rd party library and
DB. Currently the MaxMind GeoIP and MaxMindDB APIs are supported. The default is to
do a Country lookup, but the following qualifiers are supported::

    %{GEO:COUNTRY}      The country code (e.g. "US")
    %{GEO:COUNTRY-ISO}  The country ISO code (e.g. 225)
    %{GEO:ASN}          The AS number for the provider network (e.g. 7922)
    %{GEO:ASN-NAME}     A descriptive string of the AS provider

These operators can be used both as conditionals, as well as values for
setting headers. For example::

    cond %{SEND_RESPONSE_HDR_HOOK} [AND]
    cond %{GEO:COUNTRY} =US
        set-header ATS-Geo-Country %{GEO:COUNTRY}
        set-header ATS-Geo-Country-ISO %{GEO:COUNTRY-ISO}
        set-header ATS-Geo-ASN %{GEO:ASN}
        set-header ATS-Geo-ASN-NAME %{GEO:ASN-NAME}

GROUP
~~~~~
::

    cond %{GROUP}
    cond %{GROUP:END}

This condition is a pseudo condition that is used to group conditions together.
Using these groups, you can construct more complex expressions, that can mix and
match AND, OR and NOT operators. These groups are the equivalent of parenthesis
in expressions. The following pseudo example illustrates this. Lets say you want
to express::

      (A and B) or (C and (D or E))

Assuming A, B, C, D and E are all valid conditions, you would write this as::

    cond %{GROUP} [OR]
        cond A [AND]
        cond B
    cond %{GROUP:END}
    cond %{GROUP]
       cond C [AND]
       cond %{GROUP}
             cond D [OR]
             cond E
       cond %{GROUP:END}
    cond %{GROUP:END}

Here's a more realistic example, abeit constructed, showing how to use the
groups to construct a complex expression with real header value comparisons::

    cond %{SEND_REQUEST_HDR_HOOK} [AND]
    cond %{GROUP} [OR]
        cond %{CLIENT-HEADER:X-Bar} /foo/ [AND]
        cond %{CLIENT-HEADER:User-Agent} /Chrome/
    cond %{GROUP:END}
    cond %{GROUP}
        cond %{CLIENT-HEADER:X-Bar} /fie/ [AND]
        cond %{CLIENT-HEADER:User-Agent} /MSIE/
    cond %{GROUP:END}
        set-header X-My-Header "This is a test"

Note that the ``GROUP`` and ``GROUP:END`` conditions do not take any operands per se,
and you are still limited to operations after the last condition. Also, the ``GROUP:END``
condition must match exactly with the last ``GROUP`` conditions, and they can be
nested in one or several levels.

When closing a group with ``GROUP::END``, the modifiers are not used, in fact that entire
condition is discarded, being used only to close the group. You may still decorate it
with the same modifier as the opening ``GROUP`` condition, but it is not necessary.

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

HTTP-CNTL
~~~~~~~~~
::

    cond %{HTTP-CNTL:<controller>}

This condition allows you to check the state of various HTTP controls. The controls
are of the same name as for `set-http-cntl`_. This condition returns a ``true`` or
``false`` value, depending on the state of the control. For example::

    cond %{HTTP-CNTL:LOGGING} [NOT]

would only continue evaluation if logging is turned off.

.. _admin-plugins-header-rewrite-id:

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

.. _admin-plugins-header-rewrite-inbound:

INBOUND
~~~~~~~
::

   cond %{INBOUND:TLS} /./

This condition provides access to information about the inbound (client, user agent) connection to ATS.
The data that can be checked is ::

   %{INBOUND:LOCAL-ADDR}      The local (ATS) address for the connection. Equivalent to %{IP:INBOUND}.
   %{INBOUND:LOCAL-PORT}      The local (ATS) port for the connection.
   %{INBOUND:REMOTE-ADDR}     The client address for the connection. Equivalent to %{IP:CLIENT}.
   %{INBOUND:REMOTE-PORT}     The client port for the connection.
   %{INBOUND:TLS}             The TLS protocol if the connection is over TLS, otherwise the empty string.
   %{INBOUND:H2}              The string "h2" if the connection is HTTP/2, otherwise the empty string.
   %{INBOUND:IPV4}            The string "ipv4" if the connection is IPv4, otherwise the empty string.
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

As a special matcher, the inbound IP addresses can be matched against a list of IP ranges, e.g.
::

   cond %{INBOUND:REMOTE-ADDR} {192.168.201.0/24,10.0.0.0/8}

.. note::
    This will not work against the non-IP based conditions, such as the protocol families,
    and the configuration parser will error out. The format here is very specific, in particular no
    white spaces are allowed between the ranges.

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

Note that both ``%{IP:SERVER}`` and ``%{IP:OUTBOUND}`` can be unset, in which case the
empty string is returned. The common use for this condition is
actually as a value to an operator, e.g. ::

   cond %{SEND_RESPONSE_HDR_HOOK}
     set-header X-Client-IP %{IP:CLIENT}
     set-header X-Inbound-IP %{IP:INBOUND}
     set-header X-Server-IP %{IP:SERVER}
     set-header X-Outbound-IP %{IP:OUTBOUND}

As a special matcher, the `IP` can be matched against a list of IP ranges, e.g.
::

   cond %{IP:CLIENT} {192.168.201.0/24,10.0.0.0/8}

The format here is very specific, in particular no white spaces are allowed between the
ranges.

INTERNAL-TRANSACTION
~~~~~~~~~~~~~~~~~~~~
::

    cond %{INTERNAL-TRANSACTION}

Returns true if the current transaction was internally-generated by |TS| (using
:func:`TSHttpTxnIsInternal`). These transactions are not initiated by
external client requests, but are triggered (often by plugins) entirely within
the |TS| process.

LAST-CAPTURE
~~~~~~~~~~~~
::

    cond %{LAST-CAPTURE:<part>} <operand>

If a previous condition has been a regular expression match, and capture groups
were used in the match, this condition can be used to access the last capture
group. The ``<part>`` is the index of the capture group, starting at ``0``
with a max index of ``9``. The index ``0`` is special, just like in PCRE, having
the value of the entire match.

If there was no regex match in a previous condition, these conditions have the
implicit value of an empty string. The capture groups are also only available
within a rule, and not across rules.

This condition may be most useful as a value to an operand, such as::

    cond %{HEADER:X-Foo} /foo-(.*)/
      set-header X-Foo %{LAST-CAPTURE:1}

METHOD
~~~~~~~
::

    cond %{METHOD} <operand>

The HTTP method (e.g. ``GET``, ``HEAD``, ``POST``, and so on) used by the
client for this transaction.

NEXT-HOP
~~~~~~~~
::

    cond %{NEXT-HOP:<part>} <operand>

Returns next hop current selected parent information.  The following qualifiers
are supported::

    %{NEXT-HOP:HOST} Name of the current selected parent.
    %{NEXT-HOP:PORT} Port of the current selected parent.

Note that the ``<part>`` of NEXT-HOP will likely not be available unless
an origin server connection is attempted at which point it will available
as part of the ``SEND_REQUEST_HDR_HOOK``.

For example::

    cond %{SEND_REQUEST_HDR_HOOK} [AND]
    cond %{NEXT-HOP:HOST} =www.firstparent.com
        set-header Host vhost.firstparent.com

    cond %{SEND_REQUEST_HDR_HOOK} [AND]
    cond %{NEXT-HOP:HOST} =www.secondparent.com
        set-header Host vhost.secondparent.com

.. _admin-plugins-header-rewrite-now:

NOW
~~~
::

    cond %{NOW:<part>} <operand>

This is the current time, in the local timezone as set on the machine,
typically GMT. Without any further qualifiers, this is the time in seconds
since EPOCH aka Unix time. Qualifiers can be used to give various other
values, such as year, month etc.
::

    %{NOW:YEAR}      Current year (e.g. 2016)
    %{NOW:MONTH}     Current month (0-11, 0 == January)
    %{NOW:DAY}       Current day of the month (1-31)
    %{NOW:HOUR}      Current hour (0-23, in the 24h system)
    %{NOW:MINUTE}    Current minute (0-59}
    %{NOW:WEEKDAY}   Current weekday (0-6, 0 == Sunday)
    %{NOW:YEARDAY}   Current day of the year (0-365, 0 == Jan 1st)


RANDOM
~~~~~~
::

    cond %{RANDOM:<n>} <operand>

Generates a random integer from ``0`` up to (but not including) ``<n>``. Mathematically, ``[0,n)`` or ``0 <= r < n``.

STATE-FLAG
~~~~~~~~~~
::

      cond %{STATE-FLAG:<n>}

This condition allows you to check the state of a flag. The ``<n>`` is the
number of the flag, from 0 to 15. This condition returns a ``true`` or
``false`` value, depending on the state of the flag. The default value of
all flags are ``false``.

STATE-INT8
~~~~~~~~~~
::

      cond %{STATE-INT8:<n>}

This condition allows you to check the state of an 8-bit unsigned integer.
The ``<n>`` is the number of the integer, from 0 to 3. The current value of
the state integer is returned, while all 4 integers are initialized to 0.

STATE-INT16
~~~~~~~~~~~
::

      cond %{STATE-INT16:<0>}

This condition allows you to check the state of an 16-bit unsigned integer.
There's only one such integer, and its value is returned from this condition.
As such, the index, ``0``, is optional here. The initialized value of this
state variable is ``0``.

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

SSN-TXN-COUNT
~~~~~~~~~~~~~
::

    cond %{SSN-TXN-COUNT} <operand>

Returns the number of transactions between the Traffic Server proxy and the origin server from a single session.
Any value greater than zero indicates connection reuse.

TCP-INFO
~~~~~~~~
::

    cond %{<name>}
        add-header @PropertyName "%{TCP-INFO}"

This operation records TCP Info struct field values as an Internal remap as well as global header at the event hook specified by the condition. Supported hook conditions include TXN_START_HOOK, SEND_RESPONSE_HEADER_HOOK and TXN_CLOSE_HOOK in the Global plugin and REMAP_PSEUDO_HOOK, SEND_RESPONSE_HEADER_HOOK and TXN_CLOSE_HOOK in the Remap plugin. Conditions supported as request headers include TXN_START_HOOK and REMAP_PSEUDO_HOOK. The other conditions are supported as response headers. TCP Info fields currently recorded include rtt, rto, snd_cwnd and all_retrans. This operation is not supported on transactions originated within Traffic Server (for e.g using the |TS| :func:`TSHttpTxnIsInternal`)

Condition Operands
------------------

Operands provide the means to restrict the values, provided by a condition,
which will lead to that condition evaluating as true. There are currently four
types supported:

=========== ===================================================================
Operand     Description
=========== ===================================================================
/regex/     Matches the condition's provided value against the regular
            expression. Start the regex with (?i) to flag it for a case
            insensitive match, e.g. /(?i)regex/ will match ReGeX.
(x,y,z)     Matches the condition's provided value against the list of
            comma-separated values. The list may be a list of strings, like
            ``(mp3,m3u,m3u8)``, or a list of integers, like ``(301,302,307,308)``.
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

.. note::
    Strings within a set can be quoted, but the quotes are not necessary. This
    can be important if a matching string can include spaces or commas,
    e.g. ``(foo,"bar,fie",baz)``.

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
NOCASE Indicates that the string comparison, or regular expression, should be
       case-insensitive. The default is to be case-sensitive.
PRE    Make a prefix match on a string comparison.
SUF    Make a suffix match on a string comparison.
MID    Make a substring match on a string comparison.
EXT    The substring match only applies to the file extension following a dot.
       This is generally mostly useful for the ``URL:PATH`` part.
====== ========================================================================

.. note::
    At most, one of ``[PRE]``, ``[SUF]``, ``[MID]``, or ``[EXT]`` may be
    used at any time. They can however be used together with ``[NOCASE]`` and the
    other flags.

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
advantage of `String concatenations`_ to calculate a dynamic value
for the header.

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

rm-destination
~~~~~~~~~~~~~~
::

  rm-destination <part>

Removes individual components of the remapped destination's address. When
changing the remapped destination, ``<part>`` should be used to indicate the
component that is being modified (see `URL Parts`_). Currently the only valid
parts for rm-destination are QUERY, PATH, and PORT.

For the query parameter, this operator takes an optional second argument,
which is a list of query parameters to remove (or keep with ``[INV]`` modifier).

::

  rm-destination QUERY <comma separate list of query parameter>

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

run-plugin
~~~~~~~~~~~~~~
::

  run-plugin <plugin-name>.so "<plugin-argument> ..."

This allows to run an existing remap plugin, conditionally, from within a
header rewrite rule.

set-body
~~~~~~~~
::

  set-body <text>

Sets the body to ``<text>``. Can also be used to delete a body with ``""``. This is only useful when overriding the origin status, i.e.
intercepting/pre-empting a request so that you can override the body from the body-factory with your own.

set-body-from
~~~~~~~~~~~~~
::

  set-body-from <URL>

Will call ``<URL>`` (see URL in `URL Parts`_) to retrieve a custom error response
and set the body with the result. Triggering this rule on an OK transaction will
send a 500 status code to the client with the desired response. If this is triggered
on any error status code, that original status code will be sent to the client.

.. note::
    This config should only be set using READ_RESPONSE_HDR_HOOK

An example config would look like::

   cond %{READ_RESPONSE_HDR_HOOK}
      set-body-from http://www.example.com/second

Where ``http://www.example.com/second`` is the destination to retrieve the custom response from.
This can be enabled per-mapping or globally.
Ensure there is a remap rule for the second endpoint as well!
An example remap config would look like::

   map /first http://www.example.com/first @plugin=header_rewrite.so @pparam=cond1.conf
   map /second http://www.example.com/second

set-config
~~~~~~~~~~
::

  set-config <name> <value>

Allows you to override the value of a |TS| configuration variable for the
current connection. The variables specified by ``<name>`` must be overridable.
For details on available |TS| configuration variables, consult the
documentation for :file:`records.yaml`. You can read more about overridable
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
(via :func:`TSHttpTxnDebugSet`), which causes debug messages to be printed to
the appropriate logs even when the debug tag has not been enabled. For
additional information on |TS| debugging statements, refer to
:ref:`developer-debug-tags` in the developer's documentation.

.. note::
    This operator is deprecated, use the `set-http-cntl`_ operator instead,
    with the ``TXN_DEBUG`` control.

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

The header's ``<value>`` may be a literal string, or take advantage of
`String concatenations`_ to calculate a dynamic value for the header.

set-redirect
~~~~~~~~~~~~
::

  set-redirect <code> <destination>

When invoked, sends a redirect response to the client, with HTTP status
``<code>``, and a new location of ``<destination>``. If the ``QSA`` flag is
enabled, the original query string will be preserved and added to the new
location automatically. This operator supports `String concatenations`_ for
``<destination>``.  This operator can only execute on the
``READ_RESPONSE_HDR_HOOK`` (the default when the plugin is global), the
``SEND_RESPONSE_HDR_HOOK``, or the ``REMAP_PSEUDO_HOOK``.

set-state-flag
~~~~~~~~~~~~~~
::

  set-state-flag <n> <value>

This operator allows you to set the state of a flag. The ``<n>`` is the
number of the flag, from 0 to 15. The ``<value>`` is either ``true`` or ``false``,
turning the flag on or off.

set-state-int8
~~~~~~~~~~~~~~
::

   set-state-int8 <n> <value>

This operator allows you to set the state of an 8-bit unsigned integer.
The ``<n>`` is the number of the integer, from 0 to 3. The ``<value>`` is an
unsigned 8-bit integer, 0-255. It can also be a condition, in which case the
value of the condition is used.

set-state-int16
~~~~~~~~~~~~~~~
::

   set-state-int16 0 <value>

This operator allows you to set the state of a 16-bit unsigned integer.
The ``<value>`` is an unsigned 16-bit integer as well, 0-65535. It can also
be a condition, in which case thevalue of the condition is used. The index,
0, is always required eventhough there is only one 16-bit integer state variable.

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

.. note::
    This operator is deprecated, use the `set-http-cntl`_ operator instead,
    with the ``SKIP_REMAP`` control.

set-cookie
~~~~~~~~~~
::

  set-cookie <name> <value>

Replaces the value of cookie ``<name>`` with ``<value>``, creating the cookie
if necessary.

.. _admin-plugins-header-rewrite-set-http-cntl:

set-http-cntl
~~~~~~~~~~~~~
::

  set-http-cntl <controller> <flag>

This operator lets you turn off (or on) the logging for a particular transaction.
The ``<flag>`` is any of ``0``, ``off``, and ``false``, or ``1``, ``on`` and ``true``.
The available controllers are:

================ ====================================================================
Controller       Description
================ ====================================================================
LOGGING          Turns off logging for the transaction (default: ``on``)
INTERCEPT_RETRY  Allow intercepts to be retried (default: ``off``)
RESP_CACHEABLE   Force the response to be cacheable (default: ``off``)
REQ_CACHEABLE    Force the request to be cacheable (default: ``off``)
SERVER_NO_STORE  Don't allow the response to be written to cache (default: ``off``)
TXN_DEBUG        Enable transaction debugging (default: ``off``)
SKIP_REMAP       Don't require a remap match for the transaction (default: ``off``)
================ ====================================================================

set-plugin-cntl
~~~~~~~~~~~~~~~
::

  set-plugin-cntl <controller> <value>

This operator lets you control the fundamental behavior of this plugin for a particular transaction.
The available controllers are:

================== ============================================ =======================
Controller         Operators/Conditions                         Available values
================== ============================================ =======================
TIMEZONE           ``NOW``                                      ``GMT``, or ``LOCAL``
INBOUND_IP_SOURCE  ``IP``, ``INBOUND``, ``CIDR``, and ``GEO``   ``PEER``, or ``PROXY``
================== ============================================ =======================

TIMEZONE
""""""""

This controller selects the timezone to use for ``NOW`` condition.
If ``GMT`` is set, GMT will be used regardles of the timezone setting on your system. The default value is ``LOCAL``.

INBOUND_IP_SOURCE
"""""""""""""""""
This controller selects which IP address to use for the conditions on the table above.
The default value is ``PEER`` and the IP address of the peer will be used.
If ``PROXY`` is set, and PROXY protocol is used, the source IP address provided by PROXY protocol will be used.

.. note::
    The conditions return an empty string if the source is set to ``PROXY`` but PROXY protocol header does not present.

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
I      Invert the semantics of the rules parameters
QSA    Append the results of the rule to the query string.
====== ========================================================================

String concatenations
---------------------

You can concatenate values using strings, condition values and variable expansions on the same line.

    add-header CustomHeader "Hello from %{IP:SERVER}:%{INBOUND:LOCAL-PORT}"

This is the new, generic form of setting header values. Unfortunately, string
concatenation is not yet supported in conditionals.

Note: In versions prior to ATS v9.0.0, an alternative string expansion was available. those
expansions are no longer available, but the following table can help migrations:

======================== ==========================================================================
Old expansion variable   Condition variable to use with concatenations
======================== ==========================================================================
%<proto>                 %{CLIENT-URL:SCHEME}
%<port>                  %{CLIENT-URL:PORT}
%<chi>                   %{IP:CLIENT}, %{INBOUND:REMOTE-ADDR} or e.g. %{CIDR:24,48}
%<cqhl>                  %{CLIENT-HEADER:Content-Length}
%<cqhm>                  %{METHOD}
%<cque>                  %[CLIENT-URL}
%<cquup>                 %{CLIENT-URL:PATH}
======================== ==========================================================================

.. _admin-plugins-header-rewrite-url-parts:

URL Parts
---------

Some of the conditions and operators which use a request or response URL as
their target allow for matching against specific components of the URL. For
example, the `CLIENT-URL`_ condition can be used to test just against the query
parameters by writing it as::

    cond %{CLIENT-URL:QUERY} <operand>

The URL part names which may be used for these conditions and actions are:

.. code-block::

  ┌─────────────────────────────────────────────────────────────────────────────────────────┐
  │                                          URL                                            │
  ├─────────────────────────────────────────────────────────────────────────────────────────┤
  │  https://docs.trafficserver.apache.org:443/en/latest/search.html?q=header_rewrite&...   │
  │  ┬────   ┬──────────────────────────── ┬── ─┬─────────────────── ┬───────────────────   │
  │  │       │                             │    │                    │                      │
  │  SCHEME  HOST                          PORT PATH                 QUERY                  │
  └─────────────────────────────────────────────────────────────────────────────────────────┘

======== ======================================================================
Part     Description and value for ``https://docs.trafficserver.apache.org/en/latest/search.html?q=header_rewrite``
======== ======================================================================
SCHEME   URL scheme in use (e.g. ``http`` and ``https``). ``Value`` = `https`

HOST     Full hostname. ``Value`` = `docs.trafficserver.apache.org`

PORT     Port number. (Regardless if directly specified in the URL). ``Value`` = `443`

PATH     URL substring beginning with (but not including) the first ``/`` after
         the hostname up to, but not including, the query string. **Note**: previous
         versions of ATS had a `%{PATH}` directive, this will no longer work. Instead,
         you want to use `%{CLIENT-URL:PATH}`. ``Value`` = `en/latest/search.html`

QUERY    URL substring from the ``?``, signifying the beginning of the query
         parameters, until the end of the URL. Empty string if there were no
         query parameters. ``Value`` = `  `

URL      The complete URL.  ``Value`` = `https://docs.trafficserver.apache.org/en/latest/search.html?q=header_rewrite`
======== ======================================================================

As another example, a remap rule might use the `set-destination`_ operator to
change just the hostname via::

  cond %{HEADER:X-Mobile} = "foo"
     set-destination HOST foo.mobile.bar.com

Requests vs. Responses
======================

Both HTTP requests and responses have headers, a good number of them with the
same names. When writing a rule against, for example, the ``Connection`` or
``Via`` headers which are both valid in a request and a response, how can you
tell which is which, and how do you indicate to |TS| that the condition is
specifically and exclusively for a request header or just for a response
header? And how do you ensure that a header rewrite occurs against a request
before it is proxied?

.. _hook_conditions:

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
     ATS[height=4, fontsize=10,label="{ {{<clientside0>Global:\nREAD_REQUEST_HDR_HOOK\nREAD_REQUEST_PRE_REMAP_HOOK|<clientside01>Remap rule:\nREMAP_PSEUDO_HOOK}|<clientside1>SEND_RESPONSE_HDR_HOOK}|ATS |{<originside0>SEND_REQUEST_HDR_HOOK|<originside1>READ_RESPONSE_HDR_HOOK} }",xlabel="ATS"];
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
response has been read, but prior to |TS| sending that
response to the client.

This is the default hook condition for all globally-configured rulesets.

SEND_RESPONSE_HDR_HOOK
~~~~~~~~~~~~~~~~~~~~~~

Evaluates rulesets just prior to sending the client response, but after any
cache updates may have been performed. This hook context provides a means to
modify aspects of the response sent to a client, while still caching the
original versions of those attributes delivered by the origin server.

TXN_START_HOOK
~~~~~~~~~~~~~~
Rulesets are evaluated when |TS| receives a request and accepts it. This hook context indicates that a HTTP transaction is initiated and therefore, can only be enabled as a global plugin.

TXN_CLOSE_HOOK
~~~~~~~~~~~~~~

Rulesets are evaluated when |TS| completes a transaction, i.e., after a response has been sent to the client. Therefore, header modifications at this hook condition only makes sense for internal headers.

Affected Conditions
-------------------

The following conditions are affected by the hook context in which they are
evaluated and will adjust using request or response entities automatically:

- `HEADER`_

- `METHOD`_

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
authentication headers. ::

   cond %{READ_RESPONSE_HDR_HOOK}
     rm-header Set-Cookie
     rm-header WWW-Authenticate

Count Teapots
-------------

Maintains a counter statistic, which is incremented every time an origin server
has decided to be funny by returning HTTP 418::

   cond %{READ_RESPONSE_HDR_HOOK}
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
      set-http-cntl TXN_DEBUG on

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

   cond %{READ_RESPONSE_HDR_HOOK}
   cond %{INBOUND:LOCAL-PORT} =8090
   cond %{METHOD} =HEAD
   cond %{CLIENT-HEADER:Accept-Language} /es-py/ [NOT]
   cond %{STATUS} =304 [OR]
   cond %{RANDOM:500} >290
      set-status 403

Add Cache Control Headers Based on URL Path
-------------------------------------------

This rule adds cache control headers to CDN responses based matching the
path.  One provides a max age and the other provides a "no-cache" statement to
two different file paths. ::

   cond %{SEND_RESPONSE_HDR_HOOK}
   cond %{CLIENT-URL:PATH} /examplepath1/
      add-header Cache-Control "max-age=3600"
   cond %{SEND_RESPONSE_HDR_HOOK}
   cond %{CLIENT-URL:PATH} /examplepath2\/examplepath3\/.*/
      add-header Cache-Control "no-cache"

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

Add a response header for certain status codes
----------------------------------------------

   cond %{SEND_RESPONSE_HDR_HOOK} [AND]
   cond %{STATUS} (301,302,307,308)
   set-header X-Redirect-Status %{STATUS}

Add HSTS
--------

Add the HTTP Strict Transport Security (HSTS) header if it does not exist and the inbound connection is TLS::

   cond %{READ_RESPONSE_HDR_HOOK} [AND]
   cond %{HEADER:Strict-Transport-Security} ="" [AND]
   cond %{INBOUND:TLS} ="" [NOT]
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
configuration. ::

   cond %{SEND_RESPONSE_HDR_HOOK}
   cond %{ACCESS:/path/to/the/healthcheck/file.txt}    [NOT,OR]
      set-header Connection "close"

Use Internal header to pass data
--------------------------------

In |TS|, a header that begins with ``@`` does not leave |TS|. Thus, you can use
this to pass data to different |TS| systems. For instance, a series of remap rules
could each be tagged with a consistent name to make finding logs easier. ::

   cond %{REMAP_PSEUDO_HOOK}
      set-header @PropertyName "someproperty"

(Then in :file:`logging.yaml`, log ``%<{@PropertyName}cqh>``)

.. note::
    With the new ``state-flag``, ``state-int8`` and ``state-int16`` operators, you can
    sometimes avoid setting internal ``@`` headers for passing information between hooks.
    These internal state variables are much more efficient than setting and reading headers.

Remove Client Query Parameters
------------------------------------

The following ruleset removes any query parameters set by the client. ::

   cond %{REMAP_PSEUDO_HOOK}
      rm-destination QUERY

Remove only a few select query parameters::

   cond %{REMAP_PSEUDO_HOOK}
      rm-destination QUERY foo,bar

Keep only a few select query parameters -- removing the rest::

   cond %{REMAP_PSEUDO_HOOK}
      rm-destination QUERY foo,bar [I]

Mimic X-Debug Plugin's X-Cache Header
-------------------------------------

This rule can mimic X-Debug plugin's ``X-Cache`` header by accumulating
the ``CACHE`` condition results to a header. ::

   cond %{SEND_RESPONSE_HDR_HOOK} [AND]
   cond %{HEADER:All-Cache} ="" [NOT]
      set-header All-Cache "%{HEADER:All-Cache}, %{CACHE}"

   cond %{SEND_RESPONSE_HDR_HOOK} [AND]
   cond %{HEADER:All-Cache} =""
      set-header All-Cache %{CACHE}

Add Identifier from Server with Data
------------------------------------

This rule adds an unique identifier from the server if the data is fresh from
the cache or if the identifier has not been generated yet. This will inform
the client where the requested data was served from. ::

   cond %{SEND_RESPONSE_HDR_HOOK} [AND]
   cond %{HEADER:ATS-SRVR-UUID} ="" [OR]
   cond %{CACHE} ="hit-fresh"
      set-header ATS-SRVR-UUID %{ID:UNIQUE}

Apply rate limiting for some select requests
--------------------------------------------

This rule will conditiionally, based on the client request headers, apply rate
limiting to the request. ::

   cond %{REMAP_PSEUDO_HOOK} [AND]
   cond %{CLIENT-HEADER:Some-Special-Header} ="yes"
      run-plugin rate_limit.so "--limit=300 --error=429"

Check the ``PATH`` file extension
---------------------------------

This rule will deny all requests for URIs with the ``.php`` file extension::

   cond %{REMAP_PSEUDO_HOOK} [AND]
   cond %{CLIENT-URL:PATH} ="php" [EXT,NOCASE]
      set-status 403

Use GMT regardless of system timezone setting
---------------------------------------------

This rule will change the behavior of %{NOW}. It will always return time in GMT. ::

   cond %{READ_REQUEST_HDR_HOOK}
      set-plugin-cntl TIMEZONE GMT

   cond %{SEND_RESPONSE_HDR_HOOK}
     set-header hour %{NOW:HOUR}

Use IP address provided by PROXY protocol
-----------------------------------------

This rule will change the behavior of all header_rewrite conditions which use the client's IP address on a connection.
Those will pick the address provided by PROXY protocol, instead of the peer's address. ::

   cond %{READ_REQUEST_HDR_HOOK}
      set-plugin-cntl INBOUND_IP_SOURCE PROXY

   cond %{SEND_RESPONSE_HDR_HOOK}
      set-header real-ip %{INBOUND:REMOTE-ADDR}
