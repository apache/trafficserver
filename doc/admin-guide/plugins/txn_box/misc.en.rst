.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: txnbox_common.defs

.. highlight:: yaml
.. default-domain:: txb

***************
Miscellaneous
***************

This is text that should be in the document but doesn't have a good place at the moment.

Issues
******

#. What happens to the remap requirement? What counts as a remap match? Currently adding a
   comparison "always" which always matches, so that can count as a match.

#. How optional should ``do`` nodes be? For instance, for selection cases is it OK to omit the
   node entirely if there's nothing to actually do, it's just a match for some other reason (e.g.
   to prevent requests that match from matching subsequent cases)?

   Thinking about this more, it should be possible to omit the comparison or the ``do``.

   *  If the comparison is omitted, it always matches.
   *  If the ``do`` is omitted, then nothing is done.

.. code-block:: yaml

   with: ua-req-host
   select:
   -  suffix: ".org"
      do:
      -  with: ...
         select:
         -  match: "apache"
            do: # stuff for the exact domain "apache.org"
         -  suffix: ".apache"
            do:
            -  with: ...
               select:
               -  match: "mail"
                  do: # handle mail.apache.org
               -  match: "id"
                  do: # handle id.apache.org
               # more comparisons


Working with HTTP Fields
========================

Due to history and bad practices, working with the fields in an HTTP message can be challenging.
Unfortunately this makes the configuration for these more intricate.

The general case, where there is a single field with a single value it straight forward. A field
extractor (such as :txb:ex:`proxy-req-field`) can be used to get the value, and a field directive
(such as :txb:drtv:`proxy-req-field`) to change it.

Some fields are defined to be `multi-valued <https://tools.ietf.org/html/rfc7230#section-3.2.6>`__
in which case the field value is really a list. On the other hand, there are exceptions to this
as well, most notably `Set-Cookie <https://tools.ietf.org/html/rfc6265#section-4.1>`__ which,
while multi-valued, does not follow the standard mechanism.

To handle all of these cases, the extension field of the field extractors can be used to force
the field to be handled either by value or by field. In both cases the result is a tuple to which
the standard :txb:drtv:`with` tuple handling can be used.

In addition, if the field is handled by field, there is special support in the corresponding field
directive to modifying that specific field in the message.

For illustrative purposes, the examples will use the upstream response ("ursp") extractors and
directives, but all of this applies to the client request ("creq"), proxy request ("preq"), and
proxy response ("prsp") extractors and directives in exactly the same way.

In the case of single valued field such as "Server" fields, the value is extracted using
``ursp-field<Server>` and can be changed with the directive ``ursp-field<Server>``. Here is an
example that looks at the "Transfer-Encoding" field to check for chunked encoding ::

   with: upstream-field<Transfer-Encoding>
   select:
   -  match<nc>: "chunked"
      do:
         # ... whatever

Another example is setting the "Server" field to "TxnBox-Enhanced-Server" if it is not already set
::

   upstream-field<Server>: [ upstream-field<Server> , { else: "TxnBox-Enhanced-Server" } ]

Finally, for fields like "Set-Cookie", handling must be done by the actual fields in the message
header. Here is an example that walks the "Set-Cookie" fields and if the domain is set to
"example.one", that specific field is removed. ::

   with: upstream-rsp-field<Set-Cookie>
   for-each:
   -  with: ...
      select:
      -  contains<nc>: "Domain=example.one;"
         do:
         -  upstream-field<...>: NULL

The notation "..." is a reference to the active feature, just as it as an extractor. Not all
directives support this - if one does, this is noted in the deescription along with the associated
extractor(s) that extract a corresponding active feature. In particular, a field directive of
this form can only be used with an active feature by the corresponding extractor. In this case,
``ua-req-field<...>`` would be invalid, only ``upstream-rsp-field<...>`` will work because the active feature
was extracted by the ``upstream-rsp-field`` extractor.

It's interesting to compare this to an alternative that looks similar but isn't quite the same ::

   with: upstream-rsp-field<Set-Cookie>::by-field
   select:
   -  for-all:
      -  contains<nc>: "Domain=example.one;"
         do:
         -  upstream-rsp-field<...>: NULL

The difference is ``for-all`` is a comparison and will stop at the first element that doesn't match.
``for-each`` will always do all elements. Similar differences exist if ``for-any`` or ``for-none``
are used. A way around this difference would be ::

   with: ursp-field<Set-Cookie>::by-field
   select:
   -  for-all:
      -  contains<nc>: "Domain=example.one;"
         do:
         -  ursp-field<...>: NULL
      - {} # always match

While this will work by causing every element to match, it's a bit obscure and easy to get wrong. If
every element in a tuple needs to be processed, use ``for-each``.

Examples
********

For request URLs of the form "A.apache.org/path", change to "apache.org/A/path". This example has
the full configuration to demonstrate that. Later examples have just the relevant configuration and
assume this outer wrapper is present. ::

   txn_box:
   - when: creq
     do:
     - with: ua-req-host
       select:
       -  suffix: ".apache.org"
          do:
          -  rewrite-url: "{ua-req-scheme}://apache.org/{...}{ua-req-path}"
       -  match: "apache.org"
          # Explicitly do nothing with the base host name to count as "matched".

Access Control
==============

Access to upstream resources can be controlled on a fine grained level by doing a selection and
using the :code:`deny` directive. For instance, if access to ``id.example.one`` should be restricted
to the ``GET``, ``POST``, and ``HEAD`` methods, this could be done wth ::

   with: ua-req-url
   select:
   -  match: "id.example.one"
      do:
      -  with: ua-req-method
         select:
         -  none-of:
               match: [ "GET", "POST", "HEAD" ]
            do:
               deny:
      -  proxy-req-field@Access: "Allowed" # mark OK and let the request go through.

If the method is not one of the allowed ones, the :code:`select` matches resulting in a denial.
Otherwise, because there is no match, further directives in the outside :code:`select` continue
to be performed and the transaction proceeds.

This could be done in another way ::

   with: "{creq.url}"
   select:
   -  match: "id.example.one"
      do:
      -  with: "{ua-req-method}"
         select:
         -  match: [ "GET", "POST", "HEAD" ]
            do:
               set-field: [ "Access", "Allowed" ] # mark OK and let the request go through.
      -  deny:

The overall flow is the same - if the method doesn't match an allowed, the :code:`with` is passed
over and the :code:`deny` is performed. Which of these is better depends on how much additional
processing is to be done.

In either case, the :code:`set-field` directive isn't required, it is present for illustrative
purposes. This form of the innter :code:`select` works as well.  ::

   with: "{creq.method}"
   select:
   -  not:
         match: [ "GET", "POST", "HEAD" ]
      do:
         deny:

More complex situations can be handled. Suppose all methods were to be allowed from the loopback
address. That could be done (considering just the access control :code:`select`) with ::

   with: "{inbound.remove_addr}"
   select:
   -  in: [ "127.0.0.0/8", "::1" ]
      do: # nothing
   with: "{creq.method}"
   select:
   -  not:
         match: [ "GET", "HEAD", "POST" ]
      do:
         deny:


Reverse Mapping
===============

If the URL "example.one" is rewritten to "some.place", then it useful to rewrite the ``Location``
header from "some.place" to "example.one". This could be done as ::

   with: "{ua-req-host}"
   select:
   -  match: "example.one"
      do:
      -  rewrite-url-host: "some.place"
      -  when: "send-response"
         do:
         -  with: "{prsp.field::Location}"
            select:
            -  prefix: "http://some.place"
               do:
               -  set-field: [ "Location", "http://example.com{...}" ]

Referer Remapping
=================

While there is no direct support for the old style referer remapping, it is straight forward to
achieve the same functionality using the field "Referer" extractor and selection. ::

   with: "{creq.field::Referer}"
   select:
   -  match: "" # no referrer, equivalent to the "-" notation
      do:
         deny: # must have referer
   -  suffix: [ ".example.one", ".friends.one" ]
      do:
         rewrite-url-host: "example.one"
   -  else:
      do:
         rewrite-url: "http://example.com/denied"

RECV_PORT
=========

As with referer remapping, this is easily done by extracting and selecting on the local (proxy) port.
To restrict the URL "example.one" to connections to the local port 8180 ::


   with: "{ua-req-host}"
   select:
   -  match: "example.one"
      do:
         with: "{inbound.local_port}"
         select:
         -  ne: 8180
            do:
               deny:

Note because of the no backtrack rule, the request will pass through unmodified if allowed. If it
needed to be changed to "special.example.one" that would be ::

   with: "{ua-req-host}"
   select:
   -  match: "example.one"
      do:
         with: "{inbound.local_port}"
         select:
         -  eq: 8180
            do:
               set-url-host: "special.example.on"
         -  else:
            do:
               deny:

A/B Testing
===========

The random extractor and the hash modifier are useful for doing bucket testing, where some
proportion of requests should be routed to a staging system instead of the main production upstream.
This can be done randomly per request with the random extractor, or more deterministically using the
has modifier.

Presuming the production destination is "example.one" and the test destination is "stage.example.one"
then 1% of traffic can be sent to the staging system with ::

   with: "{ua-req-host}"
   select:
   -  match: "example.com"
      do:
      -  with: "{rand::100}"
         select:
         -  eq: 1
            do:
            -  rewrite-url: "http://stage.example.com"
         -  always: # match to allow unmodified request to continue.

To be more deterministic, the bucket could be based on the client IP address. ::

   with: "{ua-req-host}"
   select:
   -  match: "example.com"
      do:
      -  with: [ "{inbound.remove_addr}", { hash: 100} ]
         select:
         -  eq: 1
            do:
            -  rewrite-url: "http://stage.example.com"
         -  always: # match to allow unmodified request to continue.

As another alternative, this could be done with a cookie in the client request. If the cookie was
"example" with the test bucket indicated by the value "test", then it would be. ::

   with: "{ua-req-host}"
   select:
   -  match: "example.com"
      do:
      -  with: "{creq.cookie::example}"
         select:
         -  match: "test"
            do:
            -  rewrite-url: "http://stage.example.com"
         -  always: # match to allow unmodified request to continue.

Real Life
=========

The following examples are not intended to be illustrative, but are based on actual production
requests from internal sources or on the mailing list. These are here to serve as a guide to
implementation.

Legacy Appliances
+++++++++++++++++

Support for legacy appliances is required. The problem is these have very old TLS stacks and among
other things do not provide SNI data. However, it is unacceptable to accept such connections in
general. The requirement is therefore to allow such connections only to specific upstream
destinations and fail the connection otherwise.

Query Only
++++++++++

Rewrite the URL iff there is a non-empty query string.

Cache TTL
+++++++++

Set a cache TTL if

*  From a specific domain.
*  There are no cache TTL control fields.

This is a bit tricky because multiple fields are involved. Could this be done via an extraction
vector, or better by doing a single extraction of all fields and checking if that is empty?

Mutual TLS
++++++++++

Control remapping based on whether the user agent provided a TLS certificate, whether the
certificate was verified, and whether the SNI name is in a whitelist.

.. rubric:: Footnotes

.. [*]

   Literals are treated internally as extractors that "extract" the literal string. In practice every
   feature string is an array of extractors.

