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

.. include:: ../../../common.defs

.. default-domain:: cpp

TSStringHtmlEscape
******************

Escape and unescape strings for inclusion in HTML.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. var:: constexpr bool TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE = true

.. function:: TSReturnCode TSStringHtmlEscape(const char * str, int str_len, char * dst, size_t dst_size, size_t * length, bool use_attribute_mode)
.. function:: TSReturnCode TSStringHtmlUnescape(const char * str, int str_len, char * dst, size_t dst_size, size_t * length)

Description
===========

:func:`TSStringHtmlEscape` escapes the UTF-8 string in :arg:`str` according to
the `HTML fragment serialization escaping algorithm
<https://html.spec.whatwg.org/#escapingString>`_. Ampersands, less-than signs,
greater-than signs, and U+00A0 NO-BREAK SPACE characters are replaced by their
HTML named character references. Pass :var:`TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE` as
:arg:`use_attribute_mode` to also replace double quotes with ``&quot;`` for use
in double-quoted HTML attribute values. Pass
:literal:`!TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE` to leave double quotes unchanged.

:func:`TSStringHtmlUnescape` is the inverse operation. It replaces ``&amp;``,
``&nbsp;``, ``&lt;``, ``&gt;``, and ``&quot;`` with the corresponding UTF-8
characters. Other named or numeric character references are left unchanged.
References are matched case-sensitively and must include the terminating
semicolon. Unescaping is performed once from left to right, so ``&amp;lt;``
becomes ``&lt;``, not ``<``.

:arg:`str_len` is the number of bytes to read from :arg:`str`. A value of
:literal:`-1` treats :arg:`str` as NUL-terminated. The caller must provide a
non-overlapping :arg:`dst` buffer whose :arg:`dst_size` includes room for the
terminating NUL. On success, :arg:`length` is set to the number of output bytes,
excluding that NUL. On failure, :arg:`length` is set to :literal:`0` if it is
not :literal:`nullptr`.

The worst-case destination size for :func:`TSStringHtmlEscape` is six times the
input length plus one byte for the terminating NUL. An input-length-plus-one
buffer is always sufficient for :func:`TSStringHtmlUnescape`.

Return Values
=============

:func:`TSStringHtmlEscape` returns :enumerator:`TS_SUCCESS` on success and
:enumerator:`TS_ERROR` if :arg:`str_len` is invalid, the escaped length
overflows, or :arg:`dst` is too small. :func:`TSStringHtmlUnescape` returns
:enumerator:`TS_ERROR` if :arg:`str_len` is invalid or :arg:`dst` is too small.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSUrlPercentEncode(3ts)`
