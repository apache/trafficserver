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

.. _admin-hrw4u:

HRW4U DSL
*********

This is a next-generation rule language for the existing ATS ``header_rewrite`` plugin. It
introduces a clearer syntax, formal grammar, full condition nesting, semantic validation,
and extensible condition/operator support.

Overview
========

HRW4U replaces the free-form text parsing of `header_rewrite` with a formally defined
grammar using ANTLR. This makes HRW4U easier to parse, validate, and extend.

Rather than repeating `header_rewrite` documentation, please refer to:
  - :ref:`admin-plugins-header-rewrite` for feature behavior and semantics
  - This page focuses on syntax and behavior *differences* in HRW4U

Why HRW4U?
----------

Over time, ``header_rewrite`` format has become unwieldy and difficult to use. Therefore
we try to improve the following:

- Structured grammar and parser
- Better error diagnostics (line/col, filename, hints)
- Proper nested condition support using `if (...)` and `{ ... }` blocks
- Symbol tables for variable declarations and usage
- Static validation of operand types and value ranges
- Explicit `VAR` declarations with typed variables (`bool`, `int8`, `int16`)
- Optional debug output to trace logic evaluation

Syntax Differences
==================

The basic structure is a `section` name defining the part of the transaction to run in
followed by conditionals and operators. It uses `if () {} else {}` conditional syntax
with `&& , || and ==`, with conditions and operators generally following function() or
object.style grammar. For instance:

.. code-block:: none

   VAR:
     Foo: bool;
     Bar: int8;

   REMAP:
   if inbound.status == 403 || access("/etc/lock") {
     inbound.req.X-Fail = "1";
   } else {
     no-op;
   }

There is no `cond` or `set-header` syntax — those are implied by context.


Condition & Operator Mapping
============================

Key Differences from header_rewrite
-----------------------------------

==================== ============================= =========================================
Feature              header_rewrite                HRW4U
==================== ============================= =========================================
Context              Context sensitive             Explicit, e.g. ``inbound`` and ``outbound``
Syntax               Free-form                     Structured `if (...) { ... }`
Conditions           `cond %{...}`                 Implicit in `if (...)`
Operators            Raw text (`set-header`)       Structured assignments or statements
Grouping             `GROUP` / `GROUP:END`         Use `()` inside `if` expressions
Else branches        `else` with indented ops      `else { ... }` with full block
Debugging            Manual with logs              Built-in debug tracing (`--debug`)
Quotes               Quoted strings optional       Quoted strings required / encouraged
Validation           Runtime                       Static during parsing and symbol resolution
==================== ============================= =========================================

.. note::
    The logical operator precedence is the same as in `header_rewrite`: which is left to right!
    This differs from most programming languages where `&&` has higher precedence than `||`.
    The implication being that you may have to use parentheses to get the desired precedence.

Conditions
----------

Below is a partial mapping of `header_rewrite` condition symbols to their HRW4U equivalents:

=============================== ================================== ================================================
Header Rewrite                   HRW4U                             Description
=============================== ================================== ================================================
cond %{ACCESS:/path}            access("/path")                    File exists at "/path" and is accessible by ATS
cond %{CACHE} =hit-fresh        cache() == "hit-fresh"             Cache lookup result status
cond %{CIDR:24,48} =ip          cidr(24,48) == "ip"                Match masked client IP address
cond %{CLIENT-HEADER:X} =foo    inbound.req.X == "foo"             Original client request header
cond %{CLIENT-URL:<C> =bar      inbound.url.<C> == "bar"           URL component match, ``C`` is ``host``, ``path`` etc.
cond %{COOKIE:foo} =bar         cookie.foo == "bar"                Check a cookie value
cond %{FROM-URL:<C>} =bar       from.url.<C> == "bar"              Remap ``From URL`` component match, ``C`` is ``host`` etc.
cond %{HEADER:X} =foo           {in,out}bound.req.X == "foo"       Context sensitive header conditions
cond %{ID:UNIQUE} =...          id.UNIQUE == "..."                 Unique transaction identifier
cond %{INTERNAL-TRANSACTION}    internal()                         Check if transaction is internally generated
cond %{IP:CLIENT} ="..."        inbound.ip == "..."                Client's IP address. Same as ``inbound.REMOTE_ADDR``
cond %{IP:INBOUND} ="..."       inbound.server == "..."            ATS's IP address to which the client connected
cond %{IP:SERVER} ="..."        outbound.ip == "..."               Upstream (next-hop) server IP address
cond %{IP:OUTBOUND} ="..."      outbound.server == "..."           ATS's outbound IP address, connecting upstream
cond %{LAST-CAPTURE:<#>} ="..." capture.<#> == "..."               Last capture group from regex match (range: `0-9`)
cond %{METHOD} =GET             inbound.method == "GET"            HTTP method match
cond %{NEXT-HOP:<C>} ="bar"     outbound.url.<C> == "bar"          Next-hop URL component match, ``C`` is ``host`` etc.
cond %{NOW:<U>} ="..."          now.<U> == "..."                   Current date/time in format, ``U`` selects time unit
cond %{RANDOM:500} >250         random(500) > 250                  Random number between 0 and the specified range
cond %{SSN-TXN-COUNT} >10       ssn-txn-count() > 10               Number of transactions on server connection
cond %{TO-URL:<C>} =bar         to.url.<C> == "bar"                Remap ``To URL`` component match, ``C`` is ``host`` etc.
cond %{TXN-COUNT} >10           txn-count() > 10                   Number of transactions on client connection
cond %{URL:<C> =bar             {in,out}bound.url.<C> == "bar"     Context aware URL component match
=============================== ================================== ================================================

The conditions operating on headers and URLs are also available as operators. E.g.:

.. code-block:: none

   if inbound.req.X == "foo" {
     inbound.req.X = "bar";
   }

In general, where it makes sense for the condition to be used as an operator, it is available as an operator.
The rule of thumb is the conditional is an operator if the value is mutable.

Operators
---------

Operators in ``header_rewrite`` mapt to HRW4U as a mix of assignments and function calls.
The preference is the assignment style when appropriate.

============================= ================================= ================================================
Header Rewrite                HRW4U                             Description
============================= ================================= ================================================
counter my_stat               counter("my_stat")                Increment internal counter
rm-client-header X-Foo        inbound.req.X-Foo = ""            Remove a client request header
rm-cookie foo                 cookie.foo = ""                   Remove a cookie
rm-destination <C>            inbound.url.<C> = ""              Remove an URL component, ``C`` is path, query etc.
rm-header X-Foo               {in,out}bound.req.X-Foo = ""      Context sensitive header removal
rm-destination QUERY ...      remove_query("foo,bar")           Remove specified query keys
rm-destination QUERY ... [I]  keep_query("foo,bar")             Keep only specified query keys
run-plugin foo.so "args"      run-plugin("foo.so", "arg1", ...) Run an external remap plugin
set-body "foo"                inbound.resp.body = "foo"         Set the response body
set-body-from "https://..."   set-body-from("https://...")      Set the response body from a URL
set-config <name> 12          set-config("name", 17)            Set a configuration variable to a value
set-conn-dscp 8               inbound.conn.dscp = 8             Set the DSCP value for the connection
set-conn-mark 17              inbound.conn.mark = 17            Set the MARK value for the connection
set-cookie foo bar            cookie.foo = "bar"                Set a response cookie
set-destination <C> bar       inbound.url.<C> = "bar"           Set a URL component, ``C`` is path, query etc.
set-header X-Bar foo          inbound.req.X-Bar = "foo"         Assign a client request header
set-redirect <Code> <URL>     set-redirect(302, "https://...")  Set a redirect response
set-status 404                http.status = 404                 Set the response status code
set-status-reason "No"        http.status.reason = "no"         Set the response status reason
============================= ================================= ================================================

In addition to those operators above, HRW4U supports the following special operators without arguments:

================= ============================ ================================
Header Rewrite    HRW4U Syntax                 Description
================= ============================ ================================
no-op             no-op();                     Explicit no-op statement
set-debug         set-debug()                  Enables ATS txn debug
skip-remap        skip-remap()                 Skip remap processing (open proxy)
================= ============================ ================================

Semantics
=========

Sections
--------

All HRW4U sections start with a label such as `REMAP:`, `READ_RESPONSE:`, etc. These map
directly to `header_rewrite` hook conditions:

=============================== ======================== ================================
Header Rewrite Hook             HRW4U Section Name       Description
=============================== ======================== ================================
TXN_START_HOOK                  TXN_START                Start of transaction
READ_REQUEST_PRE_REMAP_HOOK     PRE_REMAP                Before remap processing
REMAP_PSEUDO_HOOK               REMAP                    Default remap hook
READ_REQUEST_HDR_HOOK           READ_REQUEST             After reading request from client
SEND_REQUEST_HDR_HOOK           SEND_REQUEST             Before contacting origin
READ_RESPONSE_HDR_HOOK          READ_RESPONSE            After receiving response from origin
SEND_RESPONSE_HDR_HOOK          SEND_RESPONSE            Before sending response to client
TXN_CLOSE_HOOK                  TXN_CLOSE                End of transaction
=============================== ======================== ================================

A special section `VAR:` is used to declare variables. There is no equivalent in
`header_rewrite`, where you managed the variables manually.

.. note::
    The section name is always required in HRW4U, there are no implicit or default hooks.

Groups
------

`header_rewrite` uses `GROUP` and `GROUP:END`, whereas HRW4U uses `(...)` expressions:

.. code-block:: none

   # header_rewrite
   cond %{GROUP}
     cond A [AND]
     cond B
   cond %{GROUP:END}

   # HRW4U
   if (A && B) {
     ...
   }

Condition operators
-------------------

HRW4U supports the following condition operators, which are used in `if (...)` expressions:

==================== ========================= ============================================
Operator             HRW4U Syntax              Description
==================== ========================= ============================================
==                   foo == "bar"              String or numeric equality
!=                   foo != "bar"              String or numeric inequality
>                    foo > 100                 Numeric greater than
<                    foo < 100                 Numeric less than
~                    foo ~ /pattern/           Regular expression match
!~                   foo !~ /pattern/          Regular expression non-match
in [...]             foo in ["a", "b"]         Membership in a list of values
==================== ========================= ============================================

Modifiers
---------

HRW4U supports the following modifiers for the string conditions:

============= ===============================================
Modifier      Description
============= ===============================================
EXT           Match  extension after last dot (e.g. in a path)
MID           Match substring
SUF           Match the end of a string
PRE           Match the beginning of a string
NOCASE        Case insensitive match
============= ===============================================

These can be used with both sets and equality checks, using the ``with`` keyword:

.. code-block:: none

   if inbound.req.X == "foo" with MID,NOCASE {
     ...
   }

   if inbound.url.path in ["mp3", "mp4"] with EXT,NOCASE {
     ...
   }

Running and Debugging
=====================

To run HRW4U, just install and run the hrw4u compiler:

.. code-block:: none

   hrw4u /path/to/rules.hrw4u

Run with `--debug all` to trace:

- Lexer, parser, visitor behavior
- Condition evaluations
- State and output emission

Examples
========

The examples section in :doc:`Header Rewrite <../plugins/header_rewrite.en>` are translated below
into HRW4U with their original descriptions. These are also part of the testing suite.

Remove Origin Authentication Headers
------------------------------------

The following ruleset removes any authentication headers from the origin
response before caching it or returning it to the client. This is accomplished
by setting the hook context and then removing the cookie and basic
authentication headers.::

   READ_RESPONSE:
   outbound.resp.Set-Cookie = "";
   outbound.resp.WWW-Authenticate = "";

Count Teapots
-------------

Maintains a counter statistic, which is incremented every time an origin server
has decided to be funny by returning HTTP 418::

   SEND_RESPONSE:
   if outbound.status == 418 {
       counter("plugin.header_rewrite.teapots");
   }

Normalize Statuses
------------------

For client-facing purposes only (because we set the hook context to just prior
to delivering the response back to the client, but after all processing and
possible cache updates have occurred), replaces all 4xx HTTP status codes from
the origin server with ``404``::

   SEND_RESPONSE:
   if inbound.status > 399 && inbound.status < 500 {
       inbound.status = 404;
   }

Remove Cache Control to Origins
-------------------------------

Removes cache control related headers from requests before sending them to an
origin server::

   SEND_REQUEST:
   outbound.req.Cache-Control = "";
   outbound.req.Pragma = "";

Enable Debugging Per-Request
----------------------------

Turns on |TS| debugging statements for a transaction, but only when a special
header is present in the client request::

   READ_REQUEST:
   if inbound.req.X-Debug == "supersekret" {
      set-debug();
   }

Remove Internal Headers
-----------------------

Removes special internal/development headers from origin responses, unless the
client request included a special debug header::

   READ_RESPONSE:
   if inbound.req.X-Debug != "keep" {
       outbound.resp.X-Debug-Foo = "";
       outbound.resp.X-Debug-Bar = "";
   }

Return Original Method in Response Header
-----------------------------------------

This rule copies the original HTTP method that was used by the client into a
custom response header::

   SEND_RESPONSE:
   inbound.resp.X-Original-Method = "{inbound.method}";

Useless Example From Purpose
----------------------------

Even that useless example from Purpose in the beginning of this document is
possible to accomplish::

   READ_RESPONSE:
   if inbound.url.port == 8090 && inbound.method == "HEAD" &&
          inbound.req.Accept-Language !~ /es-py/ && outbound.status == 304 ||
          random(500) > 200 {
       outbound.status = 403;
   }

Add Cache Control Headers Based on Origin Path
----------------------------------------------

This rule adds cache control headers to CDN responses based matching the origin
path.  One provides a max age and the other provides a "no-cache" statement to
two different file paths. ::

   READ_RESPONSE:
   if inbound.url.path ~ /examplepath1/ {
      outbound.resp.Cache-Control = "max-age=3600";
      break;
   }

   READ_RESPONSE:
   if inbound.url.path ~ /examplepath2\/examplepath3\/.*/ {
      outbound.resp.Cache-Control = "no-cache";
      break;
   }

Redirect when the Origin Server Times Out
-----------------------------------------

This rule sends a 302 redirect to the client with the requested URI's Path and
Query string when the Origin server times out or the connection is refused::

   TBD

Check for existence of a header
-------------------------------

This rule will modify the ``Cache-Control`` header, but only if it is not
already set to some value, and the status code is a 2xx::

   READ_RESPONSE:
   if outbound.resp.Cache-Control == "" && outbound.status > 199 && outbound.status < 300 {
      outbound.resp.Cache-Control = "max-age=600, public";
   }

Add HSTS
--------

Add the HTTP Strict Transport Security (HSTS) header if it does not exist and the inbound connection is TLS::

   READ_RESPONSE:
   if outbound.resp.Strict-Transport-Security == "" && inbound.conn.TLS != "" {
      outbound.resp.Strict-Transport-Security  = "max-age=63072000; includeSubDomains; preload";
   }

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

   SEND_RESPONSE:
   if !access("/path/to/the/healthcheck/file.txt}") {
      inbound.resp.Connection = "close";
   }

Use Internal header to pass data
--------------------------------

In |TS|, a header that begins with ``@`` does not leave |TS|. Thus, you can use
this to pass data to different |TS| systems. For instance, a series of remap rules
could each be tagged with a consistent name to make finding logs easier.::

   REMAP:
   inbound.req.@PropertyName = "someproperty";

(Then in :file:`logging.yaml`, log ``%<{@PropertyName}cqh>``)

.. note::
    Remember using the variables in ``HRW4u`` can often be more efficient than using
    these internal headers.

Remove Client Query Parameters
------------------------------------

The following ruleset removes any query parameters set by the client.::

   REMAP:
   inbound.url.query = "";

Remove only a few select query parameters::

   REMAP:
   remove_query("foo,bar");

Keep only a few select query parameters -- removing the rest::

   REMAP:
   keep_query("foo,bar");

Mimic X-Debug Plugin's X-Cache Header
-------------------------------------

This rule can mimic X-Debug plugin's ``X-Cache`` header by accumulating
the ``CACHE`` condition results to a header.::

   SEND_RESPONSE:
   if inbound.resp.All-Cache != "" {
       inbound.resp.All-Cache = "{inbound.resp.All-Cache}, {cache()}";
   }

   SEND_RESPONSE:
   if inbound.resp.All-Cache == "" {
       inbound.resp.All-Cache = "{cache()}";
   }

And finally, a  much more efficient solution, using the ``else`` clause.::

   SEND_RESPONSE:
   if inbound.resp.All-Cache == "" {
       inbound.resp.All-Cache = "{cache()}";
   } else {
       inbound.resp.All-Cache = "{inbound.resp.All-Cache}, {cache()}";
   }

Add Identifier from Server with Data
------------------------------------

This rule adds an unique identifier from the server if the data is fresh from
the cache or if the identifier has not been generated yet. This will inform
the client where the requested data was served from.::

   SEND_RESPONSE:
   if inbound.resp.ATS-SRVR-UUID == "" || cache() == "hit-fresh" {
       inbound.resp.ATS-SRVR-UUID = "{id(UNIQUE)}";
   }

Apply rate limiting for some select requests
--------------------------------------------

This rule will conditiionally, based on the client request headers, apply rate
limiting to the request.::

   REMAP:
   if inbound.req.Some-Special-Header == "yes" {
      run-plugin("rate_limit.so", "--limit=300", "--error=429");
   }

References
==========

- :ref:`admin-plugins-header-rewrite`
