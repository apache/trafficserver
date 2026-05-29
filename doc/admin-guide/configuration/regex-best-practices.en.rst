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

.. _admin-regex-best-practices:

Writing Secure Regex Rules
**************************

Several |TS| configuration files and plugins accept regular expressions
for matching incoming requests. When the regex result drives a security
or routing decision (rule selection, ACL allow/deny, signature
exclusion, parent selection, SNI routing, and so on), the way the
operator writes the regex matters: an unanchored or partially-anchored
pattern can match more inputs than the operator intended, including
inputs crafted by clients to fire a rule that was meant to apply to
something else.

This page documents:

- the regex matching contract used at security-sensitive call sites,
- the input subject each call site matches against,
- common pitfalls that produce over-matching patterns, and
- recommended pattern shapes for each site.

.. contents::
   :local:
   :depth: 2

The matching contract
=====================

|TS| uses PCRE2 for regex matching. Patterns are compiled once at
config load and reused per request. By default, a successful match
means the pattern matched **some substring** of the input subject —
possibly the whole subject, possibly a prefix, possibly a fragment in
the middle. The matcher does not, by default, require the pattern to
consume the entire subject.

This is the standard PCRE behavior. It has security implications when
the subject is influenced by a client (a request URL, a host header,
an SNI value, a Referer header, etc.). An operator who writes the
pattern ``cdn\.example\.com`` as a host regex thinking *"match this
exact host"* will find the rule firing on client-supplied hosts like
``cdn.example.com.other.org``, because the substring
``cdn.example.com`` appears at position 0 of the longer subject.

The fix at the operator level is to anchor patterns explicitly so the
pattern says exactly what it should match:

- ``^pattern$`` — match exactly the full input
- ``^pattern`` — match anything starting with ``pattern``
- ``pattern$`` — match anything ending with ``pattern``
- ``^.*pattern.*$`` — match anything containing ``pattern`` (when
  substring matching is the actual intent)

Anchored patterns produce predictable behavior regardless of the
matcher's defaults at any given site. Unanchored or single-anchored
patterns may behave one way today and another way after future
changes; the only durable approach is to write patterns whose intent
is unambiguous from the pattern itself.

Subjects by call site
=====================

The "subject" — the string the regex is matched against — varies by
site. Knowing the subject is essential for writing correct patterns,
because the same regex against different subjects gives different
results.

remap.config — ``regex_map``
----------------------------

**Subject:** request host (no scheme, port, or path).

For URL ``http://cdn.example.com:8080/path``, the regex sees the
subject ``cdn.example.com``.

remap.config — ``map_with_referer``
-----------------------------------

**Subject:** the value of the ``Referer`` HTTP header (header value
only, not including the ``Referer:`` field name or trailing CRLF).

For header ``Referer: https://www.partner.com/page``, the regex sees
the subject ``https://www.partner.com/page``.

parent.config — ``url_regex``
-----------------------------

**Subject:** the full request URL including scheme, host, and path.

For URL ``http://example.com/news/politics/today``, the regex sees the
subject ``http://example.com/news/politics/today``.

cache.config — ``host_regex``
-----------------------------

**Subject:** request host (no scheme, port, or path) — same as
``regex_map``.

cache.config — ``url_regex``
----------------------------

**Subject:** the full request URL — same as ``parent.config``'s
``url_regex``.

splitdns.config — ``url_regex``
-------------------------------

**Subject:** the full request URL — same as ``parent.config``'s
``url_regex``.

url_sig — ``excl_regex``
------------------------

**Subject:** the full request URL, sliced before the first ``?`` or
``#``.

For URL ``http://host/path?query``, the regex sees the subject
``http://host/path``.

maxmind_acl — country regex
---------------------------

**Subject:** ``host + "/" + path`` (no scheme, no query string).

For URL ``http://example.com/file.txt``, the regex sees the subject
``example.com/file.txt``.

geoip_acl — country regex
-------------------------

**Subject:** the request URL path returned by ``TSUrlPathGet``, which
does **not** include the leading ``/``.

For URL ``http://example.com/song.mp3``, the regex sees the subject
``song.mp3``. For URL ``http://example.com/foo/song.mp3``, the regex
sees the subject ``foo/song.mp3``.

tls_bridge — SNI routing
------------------------

**Subject:** the SNI value from the inbound TLS ClientHello.

Patterns at this site are start-anchored at compile time by the
plugin, so prefix injection is blocked. Operators should still add the
end-anchor ``$`` to block trailing-content matches.

Common pitfalls
===============

Bare-token patterns without anchors
-----------------------------------

A pattern like ``\.pdf`` without any anchor matches anywhere in the
input. Against the subject ``http://host/protected.pdf.alternate``,
the substring ``.pdf`` appears at the expected position, so the rule
fires — even though the URL does not actually end in ``.pdf``.

**Recommended:** ``.*\.pdf$`` (suffix anchor) for "URLs ending in
``.pdf``", or ``^http://[^?#]*\.pdf$`` (full anchor) for full-URL
matching against a subject that includes the scheme.

DNS-label boundaries
--------------------

A pattern like ``cdn\.example\.com`` matches anywhere in the host
subject. Against ``cdn.example.com.other.org``, the substring matches
at position 0 and the rule fires — but the operator probably meant
"the exact host ``cdn.example.com``."

**Recommended:** ``^cdn\.example\.com$`` for an exact-host match, or
``^.*\.example\.com$`` for "any subdomain of ``example.com``." Note
that simply adding the start-anchor ``^`` is not enough:
``^cdn\.example\.com`` (start-anchored only) still matches
``cdn.example.com.other.org`` because the start matches and the end
is unanchored.

Forgetting that the subject excludes the scheme
-----------------------------------------------

For ``regex_map``, ``cache.config`` ``host_regex``, ``maxmind_acl``,
and ``geoip_acl``, the subject does **not** include the URL scheme
(``http://``). Patterns that try to match a leading ``http://`` will
never fire at these sites; check the *Subjects by call site* section
above before writing the pattern.

Forgetting that ``geoip_acl`` strips the leading slash
------------------------------------------------------

For ``geoip_acl``, the subject is the URL path returned by
``TSUrlPathGet``, which strips the leading ``/``. A pattern like
``/songs/.*\.mp3`` will never fire against this subject; use
``^songs/.*\.mp3$`` instead.

Suffix-only anchoring may not express the full operator intent
--------------------------------------------------------------

A pattern like ``\.pdf$`` does match subjects that end in ``.pdf`` —
PCRE scans forward through the subject and the suffix anchor is
satisfied at end-of-input. The pattern works.

What the pattern does *not* do is constrain the rest of the subject:
it matches ``http://example.com/file.pdf`` and ``cdn.example.com/file.pdf``
and ``a.pdf``, regardless of the rest of the input. At sites whose
subject is path-only or host-only (for example, ``geoip_acl`` or
``cache.config`` ``host_regex``), this can match more contexts than
the operator had in mind.

For clarity, prefer patterns that document the full operator intent.
``^https?://.*\.pdf$`` is more verbose than ``\.pdf$`` but makes the
expected subject shape (an HTTP/HTTPS URL ending in ``.pdf``)
self-documenting and resistant to surprise if the same pattern is
copied to a different site whose subject shape differs.

Recommended pattern cookbook
============================

The following table gives a recommended pattern shape for each
common operator intent at each site. Replace the example tokens
(``example.com``, ``politics``, ``.pdf``, etc.) with the values for
your deployment.

remap.config — ``regex_map``
----------------------------

Subject: host only.

============================================================  =================================================================
Operator intent                                               Recommended pattern
============================================================  =================================================================
Match the exact host ``cdn.example.com``                      ``^cdn\.example\.com$``
Match any subdomain of ``example.com``                        ``^.*\.example\.com$``
Match any host containing ``example``                         ``^.*example.*$``
============================================================  =================================================================

remap.config — ``map_with_referer``
-----------------------------------

Subject: full Referer header value.

============================================================  =================================================================
Operator intent                                               Recommended pattern
============================================================  =================================================================
Match Referer values from any subdomain of ``partner.com``    ``^https?://[^/]*\.partner\.com(/.*)?$``
Match the exact Referer ``https://www.partner.com/``          ``^https://www\.partner\.com/$``
Match any Referer containing ``partner``                      ``^.*partner.*$``
============================================================  =================================================================

parent.config — ``url_regex``
-----------------------------

Subject: full URL including scheme, host, and path.

============================================================  =================================================================
Operator intent                                               Recommended pattern
============================================================  =================================================================
Match URLs whose path starts with ``/news/politics/``         ``^http://[^/]+/news/politics/.*$``
Match any URL containing ``politics``                         ``^.*politics.*$``
Match the exact URL ``http://example.com/index.html``         ``^http://example\.com/index\.html$``
============================================================  =================================================================

cache.config — ``host_regex``
-----------------------------

Subject: host only.

Use the same patterns as ``regex_map``.

cache.config — ``url_regex``
----------------------------

Subject: full URL.

Use the same patterns as ``parent.config`` ``url_regex``.

splitdns.config — ``url_regex``
-------------------------------

Subject: full URL.

Use the same patterns as ``parent.config`` ``url_regex``.

url_sig — ``excl_regex``
------------------------

Subject: full URL sliced before ``?`` or ``#``.

============================================================  =================================================================
Operator intent                                               Recommended pattern
============================================================  =================================================================
Exclude a fixed set of paths from signature checks            ``^https?://[^?#]*(/crossdomain\.xml|/clientaccesspolicy\.xml)$``
Exclude any URL ending in ``.pdf``                            ``^https?://[^?#]*\.pdf$``
Exclude every URL under ``/public/``                          ``^https?://[^?#]+/public/.*$``
============================================================  =================================================================

maxmind_acl — country regex
---------------------------

Subject: ``host + "/" + path``.

============================================================  =================================================================
Operator intent                                               Recommended pattern
============================================================  =================================================================
Match any URL ending in ``.txt``                              ``^.*\.txt$``
Match any URL under ``example.com``                           ``^example\.com/.*$``
Match a specific path ``example.com/file.txt``                ``^example\.com/file\.txt$``
============================================================  =================================================================

geoip_acl — country regex
-------------------------

Subject: URL path with leading slash stripped.

============================================================  =================================================================
Operator intent                                               Recommended pattern
============================================================  =================================================================
Match any path ending in ``.mp3``                             ``^.*\.mp3$``
Match files under ``songs/``                                  ``^songs/.*$``
Match a specific path ``songs/track.mp3``                     ``^songs/track\.mp3$``
============================================================  =================================================================

tls_bridge — SNI routing
------------------------

Subject: SNI value.

============================================================  =================================================================
Operator intent                                               Recommended pattern
============================================================  =================================================================
Match the exact SNI ``svc.example.com``                       ``^svc\.example\.com$``
Match any SNI under ``.example.com``                          ``^.*\.example\.com$``
============================================================  =================================================================

Why this matters
================

Operator-written regex rules can become security boundaries when they
gate behavior such as access control, signature verification,
upstream selection, or DNS routing. A regex that is permissive in
ways the operator did not intend can let traffic through that the
operator meant to block, route to an unintended upstream, or skip a
verification step that should have applied.

Auditing existing regex rules to confirm they match exactly what the
operator intends — and no more — is a one-time cost that pays off
in predictable rule behavior across upgrades, configuration changes,
and unexpected client inputs.
