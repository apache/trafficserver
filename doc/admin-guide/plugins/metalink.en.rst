.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. _admin-plugins-metalink:


Metalink Plugin
===============

The `Metalink` plugin implements the Metalink_ download description
format in order to try not to download the same file twice.  This
improves cache efficiency and speeds up users' downloads.

It takes standard headers and knowledge about objects in the cache and
potentially rewrites those headers so that a client will use a URL
that's already cached instead of one that isn't.  The headers are
specified in RFC 6249 (Metalink/HTTP: Mirrors and Hashes) and RFC 3230
(Instance Digests in HTTP) and are sent by various download
redirectors or content distribution networks.

A lot of download sites distribute the same files from many different
mirrors and users don't know which mirrors are already cached.  These
sites often present users with a simple download button, but the
button doesn't predictably access the same mirror, or a mirror that's
already cached.  To users it seems like the download works sometimes
(takes seconds) and not others (takes hours), which is frustrating.

An extreme example of this happens when users share a limited,
possibly unreliable internet connection, as is common in parts of
Africa for example.


How it Works
------------

When the plugin sees a response with a :mailheader:`Location: ...`
header and a :mailheader:`Digest: SHA-256=...` header, it checks if
the URL in the :mailheader:`Location` header is already cached.  If it
isn't, then it tries to find a URL that is cached to use instead.  It
looks in the cache for some object that matches the digest in the
:mailheader:`Digest` header and if it succeeds, then it rewrites the
:mailheader:`Location` header with that object's URL.

This way a client should get sent to a URL that's already cached and
won't download the file again.


Installation
------------

The `Metalink` plugin is a :term:`global plugin`.  Enable it by adding
``metalink.so`` to your :file:`plugin.config` file.  There are no
options.


Implementation Status
---------------------

The plugin implements the :c:data:`TS_HTTP_SEND_RESPONSE_HDR_HOOK`
hook to check and potentially rewrite the :mailheader:`Location` and
:mailheader:`Digest` headers after responses are cached.  It doesn't
do it before they're cached because the contents of the cache can
change after responses are cached.  It uses :c:func:`TSCacheRead` to
check if the URL in the :mailheader:`Location` header is already
cached.  In future, the plugin should also check if the URL is fresh
or not.

The plugin implements the :c:data:`TS_HTTP_READ_RESPONSE_HDR_HOOK`
hook and :ref:`a null transformation <developer-plugins-http-transformations-null-transform>`
to compute the SHA-256 digest for
content as it's added to the cache.  It uses SHA256_Init(),
SHA256_Update(), and SHA256_Final() from OpenSSL to compute the
digest, then it uses :c:func:`TSCacheWrite` to associate the digest
with the request URL.  This adds a new cache object where the key is
the digest and the object is the request URL.

To check if the cache already contains content that matches a digest,
the plugin must call :c:func:`TSCacheRead` with the digest as the key,
read the URL stored in the resultant object, and then call
:c:func:`TSCacheRead` again with this URL as the key.  This is
probably inefficient and should be improved.

An early version of the plugin scanned :mailheader:`Link: <...>;
rel=duplicate` headers.  If the URL in the :mailheader:`Location: ...`
header wasn't already cached, it scanned :mailheader:`Link: <...>;
rel=duplicate` headers for a URL that was.  The :mailheader:`Digest:
SHA-256=...` header is superior because it will find content that
already exists in the cache in every case that a :mailheader:`Link:
<...>; rel=duplicate` header would, plus in cases where the URL is not
listed among the :mailheader:`Link: <...>; rel=duplicate` headers,
maybe because the content was downloaded from a URL not participating
in the content distribution network, or maybe because there are too
many mirrors to list in :mailheader:`Link: <...>; rel=duplicate`
headers.

The :mailheader:`Digest: SHA-256=...` header is also more efficient
than :mailheader:`Link: <...>; rel=duplicate` headers because it
involves a constant number of cache lookups.  RFC 6249 requires a
:mailheader:`Digest: SHA-256=...` header or :mailheader:`Link: <...>;
rel=duplicate` headers MUST be ignored:

   If Instance Digests are not provided by the Metalink servers, the
   :mailheader:`Link` header fields pertaining to this specification
   MUST be ignored.

   Metalinks contain whole file hashes as described in Section 6, and
   MUST include SHA-256, as specified in [FIPS-180-3].


.. _Metalink:    http://en.wikipedia.org/wiki/Metalink
