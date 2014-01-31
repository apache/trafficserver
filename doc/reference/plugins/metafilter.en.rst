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

.. _metafilter-plugin:

Metalink plugin
===============

The `metalink` plugin implements the
`Metalink <http://en.wikipedia.org/wiki/Metalink>`_
protocol in order to try not to download the same file twice. This
improves cache efficiency and speeds up user downloads.

Take standard headers and knowledge about objects in the cache and
potentially rewrite those headers so that a client will use a URL
that is already cached instead of one that isn't.

The `metalink` headers are specified in :rfc:`6429` and :rfc:`3230`
and are sent by various download redirectors or content distribution
networks.

A lot of download sites distribute the same files from many different
mirrors and users don't know which mirrors are already cached. These
sites often present users with a simple download button, but the
button doesn't predictably access the same mirror, or a mirror that
is already cached. To users it seems like the download works sometimes
(takes seconds) and not others (takes hours), which is frustrating.

An extreme example of this happens when users share a limited,
possibly unreliable internet connection, as is common in parts of
Africa for example.

How it works
------------

When the `metalink` plugin sees a response with a ``Location: ...`` header and a
``Digest: SHA-256=...`` header, it checks to see if the URL in the Location
header is already cached. If it isn't, then it tries to find a URL
that is cached to use instead. It looks in the cache for some object
that matches the digest in the Digest header and if it finds
something, then it rewites the ``Location`` header with the URL from
that object.

That way a client should get sent to a URL that's already cached
and the user won't end up downloading the file again.

Installation
------------

`metalink` is a global plugin. It is enabled by adding it to your
:file:`plugin.config` file. There are no options.

Implementation Status
---------------------

The `metalink` plugin implements the ``TS_HTTP_SEND_RESPONSE_HDR_HOOK``
hook to check and potentially rewrite the ``Location: ...`` and
``Digest: SHA-256=...`` headers after responses are cached. It
doesn't do it before they're cached because the contents of the
cache can change after responses are cached.  It uses :c:func:`TSCacheRead`
to check if the URL in the ``Location: ...`` header is already
cached. In future, the plugin should also check if the URL is fresh
or not.

The plugin implements ``TS_HTTP_READ_RESPONSE_HDR_HOOK`` and a null
transform to compute the SHA-256 digest for content as it's added
to the cache, then uses :c:func:`TSCacheWrite` to associate the
digest with the request URL. This adds a new cache object where the
key is the digest and the object is the request URL.

To check if the cache already contains content that matches a digest,
the plugin must call :c:func:`TSCacheRead` with the digest as the
key, read the URL stored in the resultant object, and then call
:c:func:`TSCacheRead` again with this URL as the key. This is
probably inefficient and should be improved.

An early version of the plugin scanned ``Link: <...>; rel=duplicate``
headers. If the URL in the ``Location: ...`` header was not already
cached, it scanned ``Link: <...>; rel=duplicate`` headers for a URL
that was. The ``Digest: SHA-256=...`` header is superior because it
will find content that already exists in the cache in every case
that a ``Link: <...>; rel=duplicate`` header would, plus in cases
where the URL is not listed among the ``Link: <...>; rel=duplicate``
headers, maybe because the content was downloaded from a URL not
participating in the content distribution network, or maybe because
there are too many mirrors to list in ``Link: <...>; rel=duplicate``
headers.

The ``Digest: SHA-256=...`` header is also more efficient than ``Link:
<...>; rel=duplicate`` headers because it involves a constant number
of cache lookups. :rfc:`6249` requires a ``Digest: SHA-256=...`` header
or ``Link: <...>; rel=duplicate`` headers MUST be ignored:

    If Instance Digests are not provided by the Metalink servers, the
    Link header fields pertaining to this specification MUST be ignored.

    Metalinks contain whole file hashes as described in Section 6,
    and MUST include SHA-256, as specified in [FIPS-180-3].

