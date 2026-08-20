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

Requiring whole-subject matches
===============================

:ts:cv:`proxy.config.regex.anchor_policy_rules` controls whether |TS|
enforces whole-subject matching at the sites listed below, rather than
leaving it to each pattern.

``0`` (the default)
    Matching is unchanged. Each of these patterns that is not anchored to the
    whole subject is reported at config load with a ``[regex-anchor]`` warning
    naming the site and the pattern::

        WARNING: [regex-anchor] url_sig excl_regex pattern '\.pdf' is not
        anchored to the whole subject. See
        admin-guide/configuration/regex-best-practices.

    A pattern already written as ``^...$`` or ``^...\z`` is **not** reported, so
    the report is a worklist: rewrite what it names, reload, and it shrinks. It
    reaches empty when every gate rule is anchored, which is when enforcement
    can be enabled without changing what any rule matches.

    Anchors are counted per alternative, not per pattern. In
    ``^a\.example\.com|^b\.example\.com$`` only the last branch is end-anchored,
    so the rule still matches ``a.example.com.other.net`` even though the
    pattern ends in ``$`` — that one is reported. Anything the check cannot
    account for, such as a ``(?i)`` preamble or a ``(*...)`` verb, is reported
    rather than assumed safe.

``1``
    The pattern must consume the entire subject. A rule can no longer be
    satisfied by a pattern that matches only part of it. Each pattern the policy
    had to anchor is named at config load at ``NOTE`` severity, so a rule added
    long after the migration is visible instead of silently becoming inert. An
    already-anchored rule is not named, since the policy changed nothing for it.

Covered call sites
------------------

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Site
     - Takes effect on
     - Notes
   * - ``map_with_referer`` (:file:`remap.config`)
     - ``remap.config`` reload
     -
   * - ``*_regex`` in :file:`cache.config`, :file:`parent.config`,
       :file:`splitdns.config`
     - that file's reload
     - The report names the module, directive, file and line, since ``host_regex``
       and ``url_regex`` match different subjects.
   * - ``url_sig`` ``excl_regex``
     - ``remap.config`` reload
     -
   * - ``access_control`` URI path scope
     - ``remap.config`` reload
     - An excluded path that matched as a substring stops being excluded, so
       access control begins applying to it.
   * - ``geoip_acl``, ``maxmind_acl`` country regex
     - ``remap.config`` reload
     - ``geoip_acl`` **inverts** rather than going inert — see below.
   * - ``tls_bridge`` service regex
     - **restart only**
     - ``BridgeConfig`` loads once from plugin init with no reload hook.

``regex_map`` always requires a whole-host match and is unaffected.

.. note::

   Patterns compile when a configuration is *loaded*, and this record moves no
   file's modification time. :program:`traffic_ctl` ``config reload`` therefore
   recompiles nothing on its own: touch each affected configuration file as
   well, or restart. ``tls_bridge`` requires a restart either way.

.. warning::

   ``geoip_acl`` does not merely go inert when its pattern stops matching. With
   no regex matching, it applies ``!allow`` — the inverse of the rule's own
   default — rather than leaving the request alone. Enabling enforcement can
   therefore flip that plugin's outcome instead of neutralising the rule, so
   rewrite its patterns before enabling, or leave the setting off where
   ``geoip_acl`` is in use.

.. important::

   Enabling enforcement changes what deployed rules match, so work through the
   ``[regex-anchor]`` warnings and rewrite the patterns they name first. The
   report shrinks as you go, and an empty report means enforcement can be
   enabled without changing what any rule matches.

   Two rules make a rewrite behavior preserving:

   **Leave the deployed pattern's own text alone.** Whatever it already says
   keeps its meaning. Only add anchoring around it.

   **Add** ``.*`` **at each end that was not anchored.** A pattern that relied
   on substring matching, such as ``\.pdf`` or ``\.internal\.corp\.com``,
   otherwise stops matching entirely under enforcement and its rule becomes
   inert — which for an ACL or a signature exclusion is itself a policy change.
   Note that ``^.*\.pdf$`` is *not* the rewrite of ``\.pdf``: it means "ends
   with ``.pdf``" and silently stops matching ``/report.pdf?v=2``. Both sides
   need ``.*``.

   ==============================  ======================================
   Pattern in the report           Behavior-preserving rewrite
   ==============================  ======================================
   ``\.pdf``                       ``^.*\.pdf.*$``
   ``^cdn\.example\.com``          ``^cdn\.example\.com.*$``
   ``cdn\.example\.com$``          ``^.*cdn\.example\.com$``
   ``^a\.example\.com|^b\.ex…$``   anchor **every** alternative
   ==============================  ======================================

   A pattern already anchored at both ends, such as ``^cdn\.example\.com$`` or
   ``^https?://ex\.com/priv/.*$``, is not reported and needs no change:
   enforcement is what makes what it already says hold.

   These rewrites preserve behavior for any subject that does not contain a
   newline. A subject can contain one — a percent-encoded ``%0A`` in a request
   URL is decoded before the ``*_regex`` rules in :file:`cache.config`,
   :file:`parent.config` and :file:`splitdns.config` are matched — and because
   neither ``.`` nor ``$`` treats a newline as an ordinary byte, a rule can
   still match such a subject differently once enforcement is on. Rules whose
   subjects may contain one are worth checking individually.

.. note::

   On PCRE2 older than 10.30, |TS| implements end-of-subject anchoring by
   rewriting the pattern as ``(?:pattern)\z``, because ``PCRE2_ENDANCHORED``
   does not exist there. A pattern that begins with a *verb* such as ``(*UTF)``
   or ``(*LIMIT_MATCH=100)`` becomes invalid under that rewrite, since a verb is
   only legal at the very start of a pattern — so enabling enforcement can turn
   a pattern that loads today into a compile error, reported differently at each
   call site. Check for leading ``(*`` in gate patterns before enabling on such
   a build.

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

uri_signing — ``cdniuc`` regex match
------------------------------------

.. note::
   The "matching contract" section at the top of this document
   describes |TS|'s general regex behavior, which is implemented
   with PCRE2. This call site is the exception: ``uri_signing``
   uses the GNU ``regex.h`` library's ``re_match``, which is
   start-anchored by definition (the pattern always matches
   starting at offset 0 of the subject). A leading ``^`` in an
   issuer pattern is therefore implicit, and only the trailing
   anchor ``$`` controls whether trailing content is allowed —
   the opposite of PCRE2's default substring match.

**Subject:** the normalized request URI (scheme, authority, path, and
query — produced by the plugin's URI-normalization step).

Unlike the other sites in this document, the regex pattern at this
site is **not operator-controlled**: it is the value of an inbound
JWT's ``cdniuc`` claim, in the form ``regex:<pattern>``, and is
chosen by the token issuer.
The ATS operator is the verifier, not the author of the pattern. An
unanchored issuer pattern that names a directory prefix (for example
``https://media\.example\.com/preview/``) will accept any request URI
that *starts* with that prefix — including URIs the issuer did not
intend the token to authorize. Operators who deploy ``uri_signing``
should require their token issuers to fully anchor every ``regex:``
container with ``^...$`` and confirm the issuer rejects unanchored
patterns at token-mint time.

The CDNI URI Signing draft itself does not explicitly require any
anchoring — it states only that the URI must match the regex, leaving
"match" open to interpretation. |TS| performs start-anchoring (the
GNU ``re_match`` API used internally is start-anchored by definition)
but does not enforce end-anchoring; other CDNI relying parties may
interpret the contract differently. Fully anchored issuer patterns
are the one shape every relying party agrees on, so they remove
cross-implementation ambiguity in addition to bounding the issuer's
intended scope.

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

uri_signing — ``cdniuc`` regex match
------------------------------------

Subject: normalized request URI (full URI, including scheme, authority,
path, and query).

Patterns are issuer-authored; the table below describes shapes the
issuer should use when minting tokens, not patterns the ATS operator
edits directly.

============================================================  =================================================================
Issuer intent                                                 Recommended pattern
============================================================  =================================================================
Authorize a single fixed asset                                ``^https://media\.example\.com/clip\.m3u8$``
Authorize a single fixed asset, allow any query string        ``^https://media\.example\.com/clip\.m3u8(\?.*)?$``
Authorize every asset under ``/preview/``                     ``^https://media\.example\.com/preview/.*$``
Authorize one named asset under ``/preview/``                 ``^https://media\.example\.com/preview/clip\.m3u8$``
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
