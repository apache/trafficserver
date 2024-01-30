.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: /common.defs

.. highlight:: text

.. _expr:

Feature Expressions
*******************

A feature expression specifies how to extract a feature. At run time, if the feature is needed, the
expression is applied and the result is the feature. The feature can be any of the types

=================== ==========================================================================
Type                Description
=================== ==========================================================================
String              Text.
NULL                The null value, no value.
Integer             A signed integral value.
Boolean             A value that is ``true`` or ``false``.
IP Address          An IP address.
Duration            A time span.
Tuple               A list of features.
=================== ==========================================================================

Expressions vary from simple to very complex, but ultimately produce a feature of one of these types.

Basic Expressions
=================

Basic feature expressions are very similar to `Python format strings
<https://docs.python.org/3.4/library/string.html#format-string-syntax>`__ which contains a mixture
of literal strings and extractors. Extractors are referenced by what Python calls a "replacement
field" which are set off by braces. The underlying format structure is ::

   **{** [ *name* ] [ **:** [ *format* ] [ **:** *extension* ] ] **}**

:code:`name` specifies the extractor, :code:`format` makes it possible to control the output of the
extractor (such as width), and :code:`extension` is used to pass extra data to the extractor.

In general the result of a quoted string is a string feature. All extractors can generate text output
and so can be used in a feature expression.. As a special case, if the format expression consists of
a single extractor and no literal text then the result type is the result type of the extractor.

A feature expression can be preceded by the YAML type tag ``!literal`` in which case the string is
treated as a literal string, no extractors are invoked. Doubling braces will escape the braces and
yield a single one, the same way as for Python.

And finally, as unquoted string is parsed differently. It is checked if it can be parsed as one of
a particular set of literals and if so is treated as such.

*  The string "NULL", which yields the no value feature.
*  integer
*  boolean
*  IP address

If none of these, it is treated as an extractor, e.g. as if it had been enclosed by ``"{}"``. This
is by far the most common case and so it is useful and clearer to skip the quotes and braces.

A feature expression can include :term:`modifier`\s. These modify or transform an extracted feature.
The base expression is used to extract a feature, then the modifiers are applied to the feature
sequentially to yield the feature. |TxB| uses a `data flow
<https://en.wikipedia.org/wiki/Dataflow_programming>`__ style where most feature manipulations are
expected to be done by modifiers on expressions.

Some examples - ::

    "Delain concert"

The literal string "Delain concert". ::

   "Forwarded for {inbound-addr-remote} to {ua-req-host}"

This extracts a string that could be "Forwarded for 192.23.34.99 to apache.org". It invokes the
extractors ``inbound-addr-remote`` and ``ua-req-host`` which extract the client IP address and
the host for the client request respectively. ::

   !literal "Forwarded for {inbound-addr-remote} to {ua-req-host}"

The literal string "Forwarded for {inbound-addr-remote} to {ua-req-host}". ::

   "Forwarded for {{inbound-addr-remote}} to {{ua-req-host}}"

The literal string "Forwarded for {inbound-addr-remote} to {ua-req-host}". ::

   56

The integer constant 56. ::

   NULL

The null feature. ::

   "NULL"

The literal string "NULL". ::

   inbound-addr-remote

The extractor ``inbound-addr-remote`` which extracts an IP address feature with the value of the
client source address. This extracts a feature of type ``IP_Address``. This identical to ::

   "{inbound-addr-remote}"

Formatting
==========

The second part of an extractor supports controlling the format of the output. This is not generally
required but in some cases it is quite useful. A good example is the extractor
:txb:ex:`is-internal`. This returns a true or false value, which is in the C style mapped to 1
and 0. However, it can be changed to "true" and "false" by specifying the output as a string. ::

   proxy-req-field<Carp-Internal>: is-internal:s

Formatting is most commonly useful when setting values, such as field values. The extracted strings
can be justified, limited in width, and in particular IP addresses can be formatted in a variety of
ways.

Scoped Extractors
=================

There are sets of extractors that extract data from a :term:`scope`. A scope becomes :term:`active`
at a specific point in a configuration and remains active for all nested configuration unless
overridden by a scope of the same type. Multiple scopes of the same type nest in the standard scope
semantics, where when a scope becomes inactive the most recently active scope becomes active again.
Any scope extractors extract data from the most recent / innermost scope.

Regular Expression Scope
------------------------

If a regular expression comparison is successful matched, or explicitly applied, this creates a
scope where the capture groups are accessible via index extractors.These are numbered in the
standard way, with ``0`` meaning the entire matched string, ``1`` the first capture group, ``2`` the
second capture group, and so on. It is an error to use an index that is larger than the available
capture groups, or outside an active scope. For example if a header named "mail-check" should be set
if the host contains the domain "mail", it could be done as

.. code-block:: yaml

   with: ua-req-host
   select:
   - rxp: "^(?:(.*?)[.])?mail(?:[.](.*?))?$"
      do:
      - proxy-req-field<mail-check>: "You've got mail from {2}!"

This is a case where a feature string must be used instead of an unquoted extractor. That is ::

   proxy-req-field<mail-check>: 2

sets the field to the integer value 2, which fails. Similarly ::

   proxy-req-field<mail-check>: "2"

sets the field to the string "2". The correct usage is ::

   proxy-req-field<mail-check>: "{2}"

sets the field to the second capture group.
