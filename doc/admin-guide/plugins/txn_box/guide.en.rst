.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: ../../../common.defs
.. include:: txnbox_common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _guide:

***********
Usage Guide
***********

This section focuses on tasks rather than mechanism, to illustrate how to use the mechanisms.

Basic Tips
**********

Based on production deployment experience, there are a few general items to keep in mind.

How to "do" it
==============

Except for top level :drtv:`when` directives for global configuration, all directives are grouped
under a :code:`do` keyword in some object. This makes the most important context for a directive
which :code:`do` contains it and transitively which object contains that :code:`do`. It is that
object that will determine whether the nested directives are invoked.

For example to invoke directives conditionally, a comparison is used. The comparison contains a
:code:`do` and the directives attached to that are invoked if the comparison succeeds. Because of
YAML structuring this is very sensitive to indentation. Consider this example from actual production
use.

The goal is to shift traffic unless the query string contains the "qx" key. The original attempt was

.. code-block:: yaml
   :emphasize-lines: 1, 2, 5

   with: pre-remap-query
   select:
   -  none-of:
      -  contains: "qx="
   do:
   -  ua-req-host: "beta.service.ex"

This didn't work because the :code:`do` was in the wrong location. Because YAML nodes are order
independent this is identical to

.. code-block:: yaml
   :emphasize-lines: 1, 2, 4

   with: pre-remap-query
   do:
   -  ua-req-host: "beta.service.ex"
   select:
   -  none-of:
      -  contains: "qx="

Now the problem is clear - the traffic shifting is always done and the comparison has no directives.
The correct configuration is

.. code-block:: yaml
   :emphasize-lines: 3,5

   with: pre-remap-query
   select:
   -  none-of:
      -  contains: "qx="
      do:
      -  ua-req-host: "beta.service.ex"

The key rule here is to line up the :code:`do` with the containing object that should trigger the
directives. In this case it is the comparison :cmp:`none-of` because the traffic should be shifted
if that matches (i.e. :cmp:`contains` does *not* match). Therefore the :code:`do` should line up
with :code:`none-of` so it is triggered by :code:`none-of`. In the erroneous case the :code:`do`
lined up with :code:`with` and so was triggered by that :code:`with`. This is a bit clearer in the
example because the configuration isn't deeply nested. In production the difference between 5 levels
of indentation and 6 is not always so obvious.

.. rubric:: Summary

Always line up :code:`do` with the directive or comparison that should trigger the directives
in the :code:`do`.

Working with HTTP fields
************************

A main use of |TxB| is to manipulate the fields in the HTTP header. There are a variety of
directives and extractors, classified primarily by which HTTP message is to be modified or examined
If a particular directive or extractor is not allowed on a hook, that indicates it's not useful. For
instance, there is no use in changing anything in the client request during the "send proxy
response" hook, as it would have no observable effect. Conversely the proxy response can't be
changed during the "read client request" hook because the proxy response doesn't exist.

The four prefixes used are

============= =======================================
ua-req        User agent request (inbound to proxy)
proxy-req     Proxy request (outbound from proxy)
upstream-rsp  Upstream response (inbound to proxy)
proxy-rsp     Proxy response (outbound from proxy)
============= =======================================

The field related directives and extractors require an argument, which is the name of the HTTP
field. This name is case insensitive because the HTTP fields names are case insensitive. The value
is a feature expression which should evaluate to a string or a list of strings. A list of strings
represents a list of duplicate fields, all with the sane name but distinct values, one for each
element of the list.

To set the field "Best-Band" to the string "Delain" in the proxy request ::

   proxy-req-field<Best-Band>: "Delain"

To set the field "TLS-Source" to the SNI name and the client IP address (see :ex:`inbound-sni` and
:ex:`inbound-addr-remote`) ::

   proxy-req-field<TLS-Source>: "{inbound-sni}@{inbound-addr-remote}"

For a connection that had an SNI of "delain.nl" from the address 10.12.97.156, the proxy request
sent to the upstream would have "TLS-Source: delain.nl@10.12.97.256".

Consider the case where various requests get remapped to the same upstream host name, but the upstream
needs the value of the "Host" field from the original request. This could by copying the ``Host`` field
to the ``Org-Host`` field - ::

   proxy-req-field<Org-Host>: ua-req-field<Host>

If this was intended for debugging and therefore to be more human readable, it could be done as ::

   proxy-req-field<Org-Host>: "Original host was {ua-req-field<Host>}"

Another common use case is to have a default value. For instance, set the field "Accept-Encoding" to
"identity" if not already set. ::

   proxy-req-field<Accept-Encoding>: [ proxy-req-field<Accept-Encoding> , { else: "identity" } ]

This assigns to "Accept-Encoding" as before, but the modifier :txb:mod:`else` is applied after
retrieving the current value of that field. This modifier keeps the original value unless it's
empty, in which case it uses its own value.

Because the input is YAML, the previous example could also be written in long hand as ::

   proxy-req-field<Accept-Encoding>:
   - proxy-req-field<Accept-Encoding>
   - else: "identity"

From the |TxB| point of view, these are indistinguishable. In both cases the feature expression is
a list of an unquoted string and an object, the first treated as an extractor and the second as
modifier. Further note the extractor being the same field as the directive is happenstance - it
could be any field, or any extractor or feature expression. This is how values can be easily copied
between fields.

A field can also be removed by assigning it the :code:`NULL` value. To remove the "X-Forwarded-For"
field from the client request ::

   ua-req-field<X-Forwarded-For>: NULL

Note this is distinct from assigning the string "NULL" ::

   ua-req-field<X-Forwarded-For>: "NULL"

and not the same as assigning the empty string, such that the field is present but without a value ::

   ua-req-field<X-Forwarded-For>: ""

For a list based example consider the ``Via`` header. This can extend over multiple fields. For this
reason the extractor :code:`proxy-req-field` can return a list.

Rewriting URLs
**************

There are a number of ways to rewrite URLs in a client request. It can be done by specifying the
entire replacement URL or by changing it piecewise.

The primary directive for this in a remap invoked configuration is the :txb:drtv:`ua-req-url` directive. This always applies to
the user agent request, and takes a full URL as its value. The user agent request is updated to be to that
URL. If the existing URL is a full URL, it is changed to the URL in the value. Otherwise only
the path is copied over. If the value URL scheme is different, the request is modified to use
that scheme (e.g., if the value URL has "https://" then the proxy request will use TLS). The
"Host field is also updated to contain the host from the value URL.

For instance, to send the request to the upstream "app.txnbox" ::

   ua-req-host: "app.txnbox"

This will change the host in the URL if already present and set the "Host" field. This could also
be done as ::

   ua-req-url-host: "app.txnbox"
   ua-req-field<Host>: "app.txnbox"

The difference is this will cause the host to be in the URL regardless if it was already present.

Using Variables
***************

For each transaction, |TxB| supports a set of named variables. The names can be arbitrary strings
and the value any feature. A variable is set using the :txb:drtv:`var` directive with an
argument of the variable name and the value a feature. To set the variable "Best-Band" to "Delain" ::

   var<Best-Band>: "Delain"

To later set the field "X-Best-Band" to the value of that variable ::

   proxy-req-field<X-Best-Band>: var<Best-Band>

Note variables are not fields in the HTTP transaction, they are entirely an internal feature of
|TxB|. In the preceding example, there is only a relationship between the variable "Best-Band" and
the proxy request field "X-Best-Band" because of the explicit assignment. If either is changed
later, the other is not [#]_. Each transaction starts with no variables set, variables do not carry
over from one transaction to any other.

One common use case for variables is to cache a value in an early hook for use in a later hook. Note
there is only one transaction name space for variables and variables set in global hooks are
available in remap and vice versa. This is handy if some remap behavior should depend on the
original client request URL or host, and not on the post-remap one. This can be done, in a limited
way, with the "proxy.config.http.pristine_host_header" configuration, but that has other potential
side effects and may not be usable because of other constraints. In contrast, caching the original
host name in a variable is easy ::

   when: ua-req
   do:
      var<pristine-host>: ua-req-host

A specific use case for this is handling cross site scripting fields, where these should be set
unless the original request was to the static image server at "images.txnbox", which may have been
remapped to a different upstream shard, changing the host in the client request. This could be done
by selecting on the "pristine-host" variable and setting the cross site fields if that is not
"image.txnbox" or a subdomain of it. ::

   when: proxy-sp
   do:
      with: var<pristine-host>
      select:
      -  none-of:
         -  tld: "image.txnbox" # This domain or any subdomain
         do:
         -  proxy-rsp-field<Expect-CT>: "max-age=31536000, report-uri=\"http://csp.txnbox\""
         -  proxy-rsp-field<X-XSS-Protection>: "1; mode=block"

Variables can be used to simplify configurations, if there is a complex configuration needed in
multiple places, the results can be placed in a variable and then that variable's value used later,
avoiding much of the complexity. For instance, remap rules could set a variable as a flag to
indicate which remap rule triggered.

.. _filter-guide:

Filter Techniques
=================

The :txb:mod:`filter` modifier can be used to perform a variety of tasks. The most common is to
filter out elements in a list.

:code:`filter` can also be used as a primitive lookup table, akin to a "switch" or "case" statement.
Consider an example where a proxy request field should be set to "high" for a set of domains,
"medium" for another, and "low" for any other domain. This could be done as ::

   proxy-req-field<Priority>:
   -  ua-req-host
   -  filter:
      -  tld: "important.tld"
         replace: "high"
      -  tld: "interesting.tld"
         replace: "medium"
      -  replace: "low"

In essence, each comparison does a ``replace`` to provide the translated value, with a final
``replace`` with no comparison that matches anything not already matched.

.. rubric:: Footnotes

.. [#]

   These are similar to the "@" headers for core |TS|, but don't have any name restriction and are
   not related to any specific header. They are stored entirely inside the |TxB| plugin.
