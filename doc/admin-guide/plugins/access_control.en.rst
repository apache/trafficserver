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

.. _admin-plugins-access_control:


Access Control Plugin
*********************

Description
===========
The `access_control` plugin covers common use-cases related to providing access control to the objects stored in CDN_ cache.


Requirements and features
=========================

#. _`Cache Access Control` - cached objects to be served only to properly authenticated and authorized users.
#. _`Authorizing Multiple Requests` - all requests from a particular UA_ in a defined time period to be authenticated and authorized only once.
#. _`Multiple Page Versions` - objects may have the same name (URI) but different content for each `target audience`_ and must be served only to the corresponding `target audience`_.
#. _`Proxy Only Mode` - CDN_ proxies the request to the origin_. In case of access control failure at the Edge_, the UA_ would not be redirected to other services, the failed request would be forwarded to the origin_.

Participants
============

* _`UA` - User Agent used by a user whose requests are subject to access control.
* _`Target audience` - an application specific audience (role, group of users, etc) to which the authenticated and authorized user belongs and which defines user's permissions.
* _`CDN` / _`Edge` - Content Delivery Network, sometimes CDN and Edge are used interchangeably
* _`IdMS` - Identity Management Services
* _`DS` - Directory services
* _`Origin` / _`Application` - origin server which is part of the particular application, sometimes origin and application are used interchangeably


Design considerations and limitations
=====================================

The following are some thoughts and ideas behind the access control flow and the plugin design.


**Existing standards**

  OAuth [1]_ [2]_ and SAML [3]_ were considered looking for use cases matching standard-based solutions but none seemed to match the requirements completely, also supporting some of the mentioned standards would require a more involved design and implementation.

**Closed integration**

  Closed integration with existing `IdMS`_ and `DS`_ was considered in earlier design stages which resulted in a too specific and unnecessarily complicated access control flow, inflexible, with higher maintenance cost, possibly not scaling and not coping well with future changes.

**Cache access approval only**

  Authentication and Authorization are to be performed only by the application_ using various services like IdMS_, DS_, etc. The application_ knows more about its users, their requests and permissions and need to deal with it regardless. It is assumed that the origin_ will perform its own access control. The CDN_ is to be concerned only with the _`access approval` (the function of actually granting or rejecting access) to the objects in its own caches based on an access token provided by the application_.

**Access token**

  The access token should be a compact and self-contained token containing the information required for properly enforcing the access control. It can be extracted from HTTP headers, cookies, URI query parameters which would allow us to support various `access approval`_ use cases.

**Access token subject**

  The subject of the token in some of the use cases would signify a separate `target audience`_ and should be opaque to CDN_ which would allow CDN_ to be taken out of the authorization and authentication equation. In those use cases the `subject` will be added to the cache key as a mechanism to support `multiple page versions`_. Special considerations should be given to the cache-hit ratio and the distinct `target audience`_ count. The bigger the number of audiences the lesser the cache-hit ratio.

**Cache key modifications**

  This plugin will not modify the cache key directly but rather rely on plugins like :ref:`admin-plugins-cachekey`.

**TLS only**

  To combat the risk of man in the middle attacks, spoofing elements of the requests, unexpectedly leaking security information only TLS will be used when requesting protected assets and exchanging access tokens.


Use cases
=========

Let us say CDN_'s domain name is ``example-cdn.com`` and origin_'s domain name is ``example.com``, ``<access_token>`` is an access token value created by the origin_ and validated by CDN_. When necessary the access token is passed from the origin_ to the CDN_ by using a response header ``TokenRespHdr`` and is stored at the UA_ in a persistent cookie ``TokenCookie``.

.. uml::

        @startuml

        participant UA
        participant CDN
        participant Origin

        autonumber "<b><00>"
        UA -> CDN : GET https://example-cdn.com/path/object\n[Cookie: TokenCookie=<access_token>]
        activate CDN

        CDN->CDN:validate access_token

        alt valid access_token
          CDN -> CDN: extract access_token //subject//\nand add it to cache key
        else missing or invalid token
          CDN -> CDN: skip cache (same as cache-miss)
        end

        alt invalid access_token OR cache-miss
          alt config:use_redirect=//true//
            CDN -> Origin : HEAD https://example.com/path/object
            activate Origin
          else config:use_redirect=//false//
            CDN -> Origin : GET https://example.com/path/object
            deactivate CDN
          end

          Origin -> Origin: trigger authentication\n+ authorization flow
          note over UA,Origin #white
            Origin <=> UA authentication and authorization flow using IdMS and DS, etc.
          endnote

          alt user unauthorized
            Origin -> CDN : 401 Unauthorized
            activate CDN
            CDN -> UA : 401 Unauthorized
          else user authorized

            Origin -> Origin:create or reuse\naccess_token

            Origin -> CDN: 200 OK\nTokenRespHdr: <access_token>
            deactivate Origin

            CDN->CDN:validate access_token

            alt invalid access_token
              CDN -> UA : 520 Error
            else
              alt config:use_redirect=//true//
                CDN -> UA : 302 Redirect\nSet-Cookie: TokenCookie=<access_token>\nLocation: https://example-cdn.com/path/object
              else config:use_redirect=//false//
                CDN -> UA : 200 OK\nSet-Cookie: TokenCookie=<access_token>
              end
            end

          end
        else valid access_token AND cache-hit
          CDN -> UA : 200 OK
          deactivate CDN
        end


        @enduml



_`Use Case 1`: Proxy only mode using HTTP cookies.
--------------------------------------------------


**<01-02>**. When a request from the UA_ is received at the CDN_ the value of ``TokenCookie`` is extracted and the access token is validated (missing cookie or access token is same as invalid).

**<03>**. If the access token is valid its opaque `subject` is extracted, added to the cache key and a cache lookup is performed.

**<06>**. Missing or invalid access token or a cache-miss leads to forwarding the request to the origin_ either for user authorization and/or for fetching the object from origin_.

**<07>**. The origin_ performs authentication and authorization of the user using IdMS_, DS_, etc. All related authentication and authorization flows are out of scope of this document.

**<08-09>**. If the user is unauthorized then ``401 Unauthorized`` is passed from the origin_ to the CDN_ and then to UA_.

**<10-11>**. If the user is authorized then an access token is returned by a response header ``TokenRespHdr`` to the CDN_ and gets validated at the Edge_ before setting the ``TokenCookie``.

**<12-13>**. If the validation of the access token received from the origin_ fails the origin_ response is considered invalid and ``520 Error`` is returned to UA_.

**<15>**. If the validation of the access token received from the origin_ succeeds then the object is returned to UA_ and a ``TokenCookie`` is set to the new access token with the CDN_ response.

**<16>**. If the access token initial UA_ request is valid and there is a cache-hit the corresponding object is delivered to the UA_.

In this use case the request with a missing or invalid token is never cached (cache skipped) since we don't have or cannot trust the `subject` from the access token to do a cache lookup and since Apache Traffic Server does not have the ability to change the cache key when it receives the Origin_ response it is impossible to cache the object based on the just received new valid token from the Origin_.

All subsequent requests having a valid token will be cached normally and if the access token is valid for long enough time not caching just the first request should not be a problem, for use cases where we cannot afford not caching the first request please see `use case 2`_.



_`Use Case 2`: Proxy only mode using HTTP cookies and redirects.
----------------------------------------------------------------

This use case is similar to `use case 1`_ but makes sure all (cacheable) requests are cached (even the one that does not have a valid access token in the UA_ request).

**<05>** In case of invalid access token instead of forwarding the original HTTP request to the origin_ a ``HEAD`` request with all headers from the original request is sent to the origin_.

**<14>** When the origin_ responds with a valid access token in ``TokenRespHdr`` the CDN_ sets the ``TokenCookie`` by using a ``302 Redirect`` response with a ``Location`` header containing the URI of original UA_ request.

In this way the after the initial failure the UA_ request is repeated with a valid access token and can be safely cached in the CDN_ cache (if the object is cacheable)

The support of this use case is still not implemented.


Access token
============

The access token could contain the following claims:

* _`subject` - the subject of the access token is an opaque string provided by the application_, in `use case 1`_ and `use case 2`_ the subject signifies a `target audience`_
* _`expiration` - `Unix time <https://en.wikipedia.org/wiki/Unix_time>`_ after which the access token is not considered valid (expired)
* _`not before time` - `Unix time <https://en.wikipedia.org/wiki/Unix_time>`_ before which the access token is not considered valid (used before its time)
* _`issued at time` - `Unix time <https://en.wikipedia.org/wiki/Unix_time>`_ the access token was issued
* _`token id` - unique opaque token id for debugging and tracking assigned by the application_,
* _`version` - access token version
* _`scope` - defines the scope in which this subject is valid
* _`key id` - the key in a map or database of secrets to be used to calculate the digest
* _`signature type`  - name of the HMAC hash function / cryptographic signature scheme to be used for calculating the message digest, supported signature types: ``HMAC-SHA-256``, ``HMAC-SHA-512``, ``RSA-PSS`` (still not implemented)
* _`message digest` - the message digest that signs the access token


To make the plugin more configurable and to support more use cases various formats could be supported in the future, i.e `Named Claim` formats , `Positional Claim` formats, `JWT` [4]_, etc.

The format of the access token will be specifiable **only** through the plugin configuration by design and not the access token since migrations from one format to another during upgrades are not expected in normal circumstances.

Changes in claim names (claim positions in `Positional Claims`), in their interpretation, adding new claims, removing claims, switching from `required` to `optional` and vice versa will be handled by having a `version`_ claim in the token.

`Version`_ and `signature type`_ claims are part of the token ("user input") to allow easier migration between different versions and signature types, but they could be overridable through configuration in future versions to force the usage only to specific versions or signature types (in which case the corresponding claim could be omitted from the token and would be ignored if specified).


The following `Named Claim` format is the only one currently supported.


Query-Param-Style Named Claim format
------------------------------------

* claim names
   * ``sub`` for `subject`_, `required`
   * ``exp`` for `expiration`_, `required`
   * ``nbf`` for `not before time`_, `optional`
   * ``iat`` for `issued at time`_, `optional`
   * ``tid`` for `token id`_, `optional`
   * ``ver`` for `version`_, `optional`, defaults to ``ver=1`` if not specified.
   * ``scope`` for `scope`_, `optional`, ignored by by the current version of the plugin, still not finalized (more applications and their use cases need to be studied to finalize the format)
   * ``kid`` for `key id`_, `required` (tokens to be always signed)
   * ``st`` for `signature type`_, `optional` (default would be ``SHA-256`` if not specified)
   * ``md`` for `message digest`_ - this claim is `required` and expected to be always the last claim.
* delimiters
   * claims are separated by ``&``
   * keys and values in each claim are separated by ``=``
* notes and limitations
   * if any claim value contains ``&`` or ``=`` escaping would be necessary (i.e. through Percent-Encoding [6]_)
   * the size of the access token cannot be larger then 4K to limit the amount of data the application_ could fit in the opaque claims, in general the access token is meant to be small since it could end up stored in a cookie and be sent as part of lots and lots of requests.
   * during signing the access token payload should end with ``&md=`` and the calculated `message digest`_ would be appended directly to the payload to form the token (see the example below).


Let us say we have a user `Kermit the Frog <https://en.wikipedia.org/wiki/Kermit_the_Frog>`_ and a user `Michigan J. Frog <https://en.wikipedia.org/wiki/Michigan_J._Frog>`_ who are part of a `target audience`_ ``frogs-in-a-well`` and a user `Nemo the Clownfish <https://en.wikipedia.org/wiki/Finding_Nemo>`_ who is part of a `target audience`_ ``fish-in-a-sea``.

Both users `Kermit the Frog <https://en.wikipedia.org/wiki/Kermit_the_Frog>`_ and `Michigan J. Frog <https://en.wikipedia.org/wiki/Michigan_J._Frog>`_ will be authorized with the following access token:

.. code-block:: bash

  sub="frogs-in-a-well"   # opaque id assigned by origin
  exp="1577836800"       # token expires   : 01/01/2020 @ 12:00am (UTC)
  nbf="1514764800"       # don't use before: 01/01/2018 @ 12:00am (UTC)
  iat="1514160000"       # token issued at : 12/25/2017 @ 12:00am (UTC)
  tid="1234567890"       # unique opaque id assigned by origin (i.e UUID)
  kid="key1"             # secret corresponding to this key is 'PEIFtmunx9'


Constructing the access token using `openssl` tool (from `bash`):

.. code-block:: bash

  payload='sub=frogs-in-a-well&exp=1577836800&nbf=1514764800&iat=1514160000&tid=1234567890&kid=key1&st=HMAC-SHA-256&md='
  signature=$(echo -n ${payload} | openssl dgst -sha256 -hmac "PEIFtmunx9")
  access_token=${payload}${signature}


The application_ would create and send the access token in a response header ``TokenRespHdr``:

.. code-block:: bash

  TokenRespHdr: sub=frogs-in-a-well&exp=1577836800&nbf=1514764800&iat=1514160000&tid=1234567890&kid=key1&st=HMAC-SHA-256&md=8879af98ab6071315a7ab55e5245cbe1c106303bcc4690cbfc807a4402d11ab3


CDN_ would set a cookie ``TokenCookie``:

.. code-block:: bash

  TokenCookie=c3ViPWZyb2dzLWluLWEtd2VsbCZleHA9MTU3NzgzNjgwMCZuYmY9MTUxNDc2NDgwMCZpYXQ9MTUxNDE2MDAwMCZ0aWQ9MTIzNDU2Nzg5MCZraWQ9a2V5MSZzdD1ITUFDLVNIQS0yNTYmbWQ9ODg3OWFmOThhYjYwNzEzMTVhN2FiNTVlNTI0NWNiZTFjMTA2MzAzYmNjNDY5MGNiZmM4MDdhNDQwMmQxMWFiMw; Expires=Wed, 01 Jan 2020 00:00:00 GMT; Secure; HttpOnly


The value of the cookie is the access token provided by the origin_ encoded with a `base64url Encoding without Padding` [4]_

The following attributes are added to the `Set-Cookie` header [5]_:

* ``Secure`` - instructs the UA_ to include the cookie in an HTTP request only if the request is transmitted over a secure channel, typically HTTP over Transport Layer Security (TLS)
* ``HttpOnly`` - instructs the UA_ to omit the cookie when providing access to cookies via "non-HTTP" APIs such as a web browser API that exposes cookies to scripts
* ``Expires`` - this attribute will be set to the time specified in the `expiration`_ claim

Just for a reference the following access token would be assigned to the user `Nemo the Clownfish <https://en.wikipedia.org/wiki/Finding_Nemo>`_:

.. code-block:: bash

  sub=fish-in-a-sea&exp=1577836800&nbf=1514764800&iat=1514160000&tid=2345678901&kid=key1&st=HMAC-SHA-256&md=a43d8a46804d9e9319b7d1337007eed73daf37105f1feaae1d68567389654f88


Plugin configuration
====================

* Specify where to look for the access token
   * ``--check-cookie=<cookie_name>`` (`optional`, default:empty/unused) - specifies the name of the cookie that contains the access token, although it is optional if not specified the plugin does not perform access control since this is the only currently supported access token source.
   * ``--token-response-header=<header_name>`` (`optional`, default:empty/unused) - specifies the origin_ response header name that contains the access token passed from the origin_ to the CDN, although it is optional this is the only currently supported way to get the access token from the origin_.


* Signify some common failures through HTTP status code.
   * ``--invalid-syntax-status-code=<http_status_code>`` (`optional`, default:``400``) - access token bad syntax error
   * ``--invalid-signature-status-code=<http_status_code>`` (`optional`, default:``401``) - invalid access token signature
   * ``--invalid-timing-status-code=<http_status_code>`` (`optional`, default:``403``) - bad timing when validating the access token (expired, or too early)
   * ``--invalid-scope-status-code=<http_status_code>`` (`optional`, default:``403``) - access token scope validation failed
   * ``--invalid-origin-response=<http_status_code>`` (`optional`, default:``520``) - origin_ response did not look right, i.e. the access token provided by origin_ is not valid.
   * ``--internal-error-status-code=<http_status_code>`` (`optional`, default:``500``) - unexpected internal error (should not happened ideally)

* Extract information into a request header
   * ``--extract-subject-to-header=<header_name>`` (`optional`, default:empty/unused) - extract the access token `subject` claim into a request header with ``<header_name>`` for debugging purposes and logging or to be able to modify the cache key by using :ref:`admin-plugins-cachekey` plugin.
   * ``--extract-tokenid-to-header=<header_name>`` (`optional`, default:empty/unused) - extract the access token `token id` claim into a request header with ``<header_name>`` for debugging purposes and logging
   * ``--extract-status-to-header=<header_name>`` (`optional`, default:empty/unused) - extract the access token validation status request header with ``<header_name>`` for debugging purposes and logging


* Plugin setup related
   * ``--symmetric-keys-map=<txt_file_name>`` (`optional`, default: empty/unused) - the name of a file containing a map of symmetric encrypt secrets, secrets are expected one per line in format ``key_name_N=secret_value_N`` (key names are used in access token signature validation, multiple keys would be useful for key rotation). Although it is `optional` this is the only source of secrets supported and if not specified / used access token validation would constantly fail.
   * ``--include-uri-paths-file`` (`optional`, default:empty/unused) - a file containing a list of regex expressions to be matched against URI paths. The access control is applied to paths that match.
   * ``--exclude-uri-paths-file`` (`optional`, default:empty/unused) - a file containing a list of regex expressions to be matched against URI paths. The access control is applied to paths that do not match.

* Behavior modifiers to support various use-cases
   * ``--reject-invalid-token-requests`` (`optional`, default:``false``) - reject invalid token requests instead of forwarding them to origin_.
   * ``--use-redirects`` (`optional`, default:``false``) - used to configure `use case 2`_, not implemented yet.



Configuration and Troubleshooting examples
------------------------------------------

The following configuration can be used to implement `use case 1`_

Configuration files
~~~~~~~~~~~~~~~~~~~

* Apache traffic server ``remap.config``

:ref:`admin-plugins-cachekey` is used to add the access token `subject` into the cache key (``@TokenSubject``). and should always follow the :ref:`admin-plugins-access_control` in the remap rule in order for this mechanism to work properly.

.. code-block:: bash

  map https://example-cdn.com http://example.com \
      @plugin=access_control.so \
          @pparam=--symmetric-keys-map=hmac_keys.txt \
          @pparam=--check-cookie=TokenCookie \
          @pparam=--extract-subject-to-header=@TokenSubject \
          @pparam=--extract-tokenid-to-header=@TokenId \
          @pparam=--extract-status-to-header=@TokenStatus \
          @pparam=--token-response-header=TokenRespHdr \
      @plugin=cachekey.so \
          @pparam=--static-prefix=views \
          @pparam=--include-headers=@TokenSubject



* Secrets map ``hmac_keys.txt``

.. code-block:: bash

  $ cat etc/trafficserver/hmac_keys.txt
  key1=PEIFtmunx9
  key2=BtYjpTbH6a
  key3=SS75kgYonh
  key4=qMmCV2vUsu
  key5=YfMxMaygax
  key6=tVeuPtfJP8
  key7=oplEZT5CpB


* Format the ``access_control.log``

.. code-block:: bash

  access_control_format = format {
    Format = '%<cqtq> sub=%<{@TokenSubject}cqh> tid=%<{@TokenId}cqh> status=%<{@TokenStatus}cqh> cache=%<{x-cache}psh> key=%<{x-cache-key}psh>'  }

  log.ascii {
    Filename = 'access_control',
    Format = access_control_format
  }


* X-Debug plugin added to ``plugin.config``

.. code-block:: bash

  $ cat etc/trafficserver/plugin.config
  xdebug.so



Configuration tests and troubleshooting
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Let us assume that the origin_ responds with the access tokens considered in `Query-Param-Style Named Claim format`_ corresponding to user `Kermit the Frog <https://en.wikipedia.org/wiki/Kermit_the_Frog>`_ and user `Nemo the Clownfish <https://en.wikipedia.org/wiki/Finding_Nemo>`_

If user `Kermit the Frog <https://en.wikipedia.org/wiki/Kermit_the_Frog>`_ sends a request without a valid token, i.e ``TokenCookie`` is missing from the request. Cache key would be ``/views/object`` but never used since the cache is always ``skipped``.

After the origin_ responds with a valid access token (assuming the user authentication and authorization succeeded) the plugin will respond with ``Set-Cookie`` header containing the new access token.

.. code-block:: bash

  $ curl -sD - https://example-cdn.com/object \
      -H 'X-Debug:X-Cache, X-Cache-Key' \
    | grep -ie 'x-cache' -e 'tokencookie'

  set-cookie: TokenCookie=c3ViPWZyb2dzLWluLWEtd2VsbCZleHA9MTU3NzgzNjgwMCZuYmY9MTUxNDc2NDgwMCZpYXQ9MTUxNDE2MDAwMCZ0aWQ9MTIzNDU2Nzg5MCZraWQ9a2V5MSZzdD1ITUFDLVNIQS0yNTYmbWQ9ODg3OWFmOThhYjYwNzEzMTVhN2FiNTVlNTI0NWNiZTFjMTA2MzAzYmNjNDY5MGNiZmM4MDdhNDQwMmQxMWFiMw; Expires=Wed, 01 Jan 2020 00:00:00 GMT; Secure; HttpOnly
  x-cache-key: /views/object
  x-cache: skipped


Now let us send the same request with a valid access token, add the ``TokenCookie`` to the request. Cache key will be ``/views/@TokenSubject:frogs-in-a-well/object`` but since the object is not in the cache we get ``miss``.

.. code-block:: bash

  $ curl -sD - https://example-cdn.com/object \
      -H 'X-Debug:X-Cache, X-Cache-Key' \
      -H 'cookie: TokenCookie=c3ViPWZyb2dzLWluLWEtd2VsbCZleHA9MTU3NzgzNjgwMCZuYmY9MTUxNDc2NDgwMCZpYXQ9MTUxNDE2MDAwMCZ0aWQ9MTIzNDU2Nzg5MCZraWQ9a2V5MSZzdD1ITUFDLVNIQS0yNTYmbWQ9ODg3OWFmOThhYjYwNzEzMTVhN2FiNTVlNTI0NWNiZTFjMTA2MzAzYmNjNDY5MGNiZmM4MDdhNDQwMmQxMWFiMw; Expires=Wed, 01 Jan 2020 00:00:00 GMT; Secure; HttpOnly' \
    | grep -ie 'x-cache' -e 'tokencookie'

  x-cache-key: /views/@TokenSubject:frogs-in-a-well/object
  x-cache: miss


Now let us send the same request again and since the object is in cache we get ``hit-fresh``.


.. code-block:: bash

  $ curl -sD - https://example-cdn.com/object \
      -H 'X-Debug:X-Cache, X-Cache-Key' \
      -H 'cookie: TokenCookie=c3ViPWZyb2dzLWluLWEtd2VsbCZleHA9MTU3NzgzNjgwMCZuYmY9MTUxNDc2NDgwMCZpYXQ9MTUxNDE2MDAwMCZ0aWQ9MTIzNDU2Nzg5MCZraWQ9a2V5MSZzdD1ITUFDLVNIQS0yNTYmbWQ9ODg3OWFmOThhYjYwNzEzMTVhN2FiNTVlNTI0NWNiZTFjMTA2MzAzYmNjNDY5MGNiZmM4MDdhNDQwMmQxMWFiMw; Expires=Wed, 01 Jan 2020 00:00:00 GMT; Secure; HttpOnly' \
    | grep -ie 'x-cache' -e 'tokencookie'

  x-cache-key: /views/@TokenSubject:frogs-in-a-well/object
  x-cache: hit-fresh


The previous activity should result in the following log (as defined in ``logging.config``)

.. code-block:: bash

  1521588755.424 sub=- tid=- status=U_UNUSED,O_VALID cache=skipped key=/views/object
  1521588986.262 sub=frogs-in-a-well tid=this-year-frog-view status=U_VALID,O_UNUSED cache=miss key=/views/@TokenSubject:frogs-in-a-well
  1521589276.535 sub=frogs-in-a-well tid=this-year-frog-view status=U_VALID,O_UNUSED cache=hit-fresh key=/views/@TokenSubject:frogs-in-a-well


Just for a reference the same request for user `Nemo the Clownfish <https://en.wikipedia.org/wiki/Finding_Nemo>`_, with a different subject/target audience ``fish-in-a-sea``, will end up having cache key ``/views/@TokenSubject:fish-in-a-sea/object`` and would never match the same object cached for users in the ``frogs-in-a-well`` audience as they use cache key ``/views/@TokenSubject:frogs-in-a-well/object``.


---------------------------


References
==========

.. [1] "The OAuth 1.0 Protocol", `RFC 5849 <https://tools.ietf.org/html/rfc5849>`_, April 2010.

.. [2] "The OAuth 2.0 Authorization Framework", `RFC 6749 <https://tools.ietf.org/html/rfc6749>`_, October 2012

.. [3] "Security Assertion Markup Language 2.0 (SAML 2.0)" `OASIS <https://wiki.oasis-open.org>`_, March 2005.

.. [4] "JSON Web Signature (JWS)", Appendix C. "Notes on Implementing base64url Encoding without Padding", `RFC 7515 <https://tools.ietf.org/html/rfc7515#appendix-C>`_, May 2015

.. [5] "HTTP State Management Mechanism", 4.1 "Set-Cookie", `RFC 6225 <https://tools.ietf.org/html/rfc6265#section-4.1>`_, April 2011

.. [6] "Uniform Resource Identifier (URI): Generic Syntax", 2.1. "Percent-Encoding", `RFC 3986 <https://tools.ietf.org/html/rfc3986#section-2.1>`_, January 2005.

