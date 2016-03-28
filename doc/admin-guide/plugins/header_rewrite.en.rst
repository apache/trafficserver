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

CLIENT-IP
~~~~~~~~~
::

    cond %{CLIENT-IP} <operand>

Remote IP address, as a string, of the client connection for the current
transaction.

CLIENT-URL
~~~~~~~~~~
::

    cond %{CLIENT-URL:<part>} <operand>

The URL of the original request. Regardless of the hook context in which the
rule is evaluated, this condition will always operate on the original, unmapped
URL supplied by the client. The ``<part>`` may be specified according to the
options documented in `URL Parts`_.

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
do a Country lookup, but the following qualifiers are supported:

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

INCOMING-PORT
~~~~~~~~~~~~~
::

    cond %{INCOMING-PORT} <operand>

TCP port, as a decimal integer, on which the incoming client connection was
made.

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

The path component of the transaction. This includes the leading ``/`` that
immediately follows the hostname and terminates prior to the ``?`` signifying
the beginning of query parameters (or the end of the URL, whichever occurs
first).

Refer to `Requests vs. Responses`_ for more information on determining the
context in which the transaction's URL is evaluated.

QUERY
~~~~~
::

    cond %{QUERY} <operand>

The query parameters, if any, of the transaction.  Refer to `Requests vs.
Responses`_ for more information on determining the context in which the
transaction's URL is evaluated.

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

Returns the current HTTP client session, which permits detection of requests
which are sharing a client session. Shared client sessions occur when multiple
simultaneous requests are received for the same cache object. Instead of
contacting the origin server separately for each of those client requests, one
origin connection is used to fulfill all of the requests assigned to the shared
client session.

URL
~~~
::

    cond %{URL:option} <operand>

The complete URL of the current transaction. This will automatically choose the
most relevant URL depending upon the hook context in which the condition is
being evaluated.

Refer to `Requests vs. Responses`_ for more information on determining the
context in which the transaction's URL is evaluated.

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
advantage of `Variable Expansion`_ to calculate a dynamic value for the header.
In contrast, `set-header`_ does not support variable expansion for the header
value. If you wish to use variable expansion and avoid duplicate headers, you
may consider using an `rm-header`_ operator followed by `add-header`_.

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

Note that, unlike `add-header`_, this operator does not currently support
variable expansion. Values may only be specified according to `Header Values`_.

set-redirect
~~~~~~~~~~~~
::

  set-redirect <code> <destination>

When invoked, sends a redirect response to the client, with HTTP status
``<code>``, and a new location of ``<destination>``. If the ``QSA`` flag is
enabled, the original query string will be preserved and added to the new
location automatically. This operator supports `Variable Expansion`_ for
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

Variable Expansion
------------------

Only limited variable expansion is supported in `add-header`_. Supported
substitutions are currently:

============ ==================================================================
Variable     Description
============ ==================================================================
%<proto>     Protocol
%<port>      Port
%<chi>       Client IP
%<cqhl>      Client request length
%<cqhm>      Client HTTP method
%<cquup>     Client unmapped URI
============ ==================================================================

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
PATH     URL substring beginning with the first ``/`` after the hostname up to,
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

READ_RESPONSE_HDR_HOOK
~~~~~~~~~~~~~~~~~~~~~~

Rulesets evaluated within this context will process only once the origin server
response (or cached response) has been read, but prior to |TS| sending that
response to the client.

This is the default hook condition for all globally-configured rulesets.

READ_REQUEST_HDR_HOOK
~~~~~~~~~~~~~~~~~~~~~

Evaluates as soon as the client request has been read, but prior to any further
processing (including contacting origin servers or fetching objects from cache).
Conditions and operators which adapt to matching or manipulating request or
response entities (e.g. headers) depending on their context will all operate on
the request variants when using this hook, as there is no response data yet.

READ_REQUEST_PRE_REMAP_HOOK
~~~~~~~~~~~~~~~~~~~~~~~~~~~

For ruleset configurations provided via :file:`remap.config`, this forces their
evaluation as soon as the request has been read, but prior to the remapping.
For all context-adapting conditions and operators, matching will occur against
the request, as there is no response data available yet.

REMAP_PSEUDO_HOOK
~~~~~~~~~~~~~~~~~

This is the default hook condition for all rulesets configured via remapping
rules in :file:`remap.config`. Functionally equivalent to
`READ_RESPONSE_HDR_HOOK`_ in that rulesets will evaluate after responses from
origin servers have been received (or the object has been retrieved from
cache), but prior to sending the client response.

What sets this hook context apart is that in configuration files shared by both
the global :file:`plugin.config` and individual remapping entries in
:file:`remap.config`, this hook condition will force the subsequent ruleset(s)
to be valid only for remapped transactions.

SEND_REQUEST_HDR_HOOK
~~~~~~~~~~~~~~~~~~~~~

Forces evaluation of the ruleset just prior to contacting origin servers (or
fetching the object from cache), but after any remapping may have occurred.

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

    cond %{INCOMING-PORT} =8090
    cond %{METHOD} =HEAD
    cond %{CLIENT-HEADER:Accept-Language} /es-py/ [NOT]
    cond %{STATUS} =304 [OR]
    cond %{RANDOM:500} >290
    set-status 403

Add Cache Control Headers Based on Origin Path
----------------------------------------------

This rule adds cache control headers to CDN responses based matching the origin
path.  One provides a max age and the other provides a “no-cache” statement to
two different file paths.::

    cond %{SEND_RESPONSE_HDR_HOOK}
    cond %{PATH} /examplepath1/
    add-header Cache-Control "max-age=3600" [L]
    cond %{SEND_RESPONSE_HDR_HOOK}
    cond %{PATH} /examplepath2/examplepath3/.*/
    add-header Cache-Control "no-cache" [L]
