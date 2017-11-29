URI Signing Plugin
==================

This remap plugin implements the draft URI Signing protocol documented here:
https://tools.ietf.org/html/draft-ietf-cdni-uri-signing-12 .

It takes a single argument: the name of a config file that contains key information.

**Nota bene:** Take care in ordering the plugins. In general, this plugin
should be first on the remap line. This is for two reasons. First, if no valid
token is present, it is probably not useful to continue processing the request
in future plugins.  Second, and more importantly, the signature should be
verified _before_ any other plugins modify the request. If another plugin drops
or modifies the query string, the token might be missing entirely by the time
this plugin gets the URI.

Config
------

The config file should be a JSON object that maps issuer names to JWK-sets.
Exactly one of these JWK-sets must have an additional member indicating the
renewal key.

    {
      "Kabletown URI Authority": {
        "renewal_kid": "Second Key",
        "keys": [
          {
            "alg": "HS256",
            "kid": "First Key",
            "kty": "oct",
            "k": "Kh_RkUMj-fzbD37qBnDf_3e_RvQ3RP9PaSmVEpE24AM"
          },
          {
            "alg": "HS256",
            "kid": "Second Key",
            "kty": "oct",
            "k": "fZBpDBNbk2GqhwoB_DGBAsBxqQZVix04rIoLJ7p_RlE"
          }
        ]
      }
    }

If there is not precisely one renewal key, the plugin will not load.

Although the `kid` and `alg` parameters are optional in JWKs generally, both
members must be present in keys used for URI signing.

Usage
-----

The URI signing plugin will block all requests that do not bear a valid JWT, as
defined by the URI Signing protocol. Clients that do not present a valid JWT
will receive a 403 Forbidden response, instead of receiving content.

Tokens will be found in either of these places:

  - A query parameter named `URISigningPackage`. The value must be the JWT.
  - A cookie named `URISigningPackage`. The value of the cookie must be the JWT.

Path parameters will not be searched for JWTs.

### Supported Claims

The following claims are understood:

  - `iss`: Must be present. The issuer is used to locate the key for verification.
  - `sub`: Validated last, after key verification. **Only `uri-regex` is supported!**
  - `exp`: Expired tokens are not valid.
  - `iat`: May be present, but is not validated.
  - `cdniv`: Must be missing or 1.
  - `cdnistt`: If present, must be 1.
  - `cdniets`: If cdnistt is 1, this must be present and non-zero.

### Unsupported Claims

These claims are not supported. If they are present, the token will not validate:

  - `aud`
  - `nbf`
  - `jti`

In addition, the `sub` containers of `uri`, `uri-pattern`, and `uri-hash` are
**not supported**.

### Token Renewal

If the `cdnistt` and `cdniets` claims are present, the token will be renewed.
The new token will be returned via a `Set-Cookie` header as a session cookie.

However, instead of setting the expiration to be `cdniets` seconds from the
expiration of the previous cookie, it is set to `cdniets` seconds from the time
it was validated. This is to prevent a crafty client from repeatedly renewing
tokens in quick succession to create a super-token that lasts long into the
future, thereby circumventing the intent of the `exp` claim.

### JOSE Header

The JOSE header of the JWT should contain a `kid` parameter. This is used to
quickly select the key that was used to sign the token. If it is provided, only
the key with a matching `kid` will be used for validation. Otherwise, all
possible keys for that issuer must be tried, which is considerably more
expensive.

Building
--------

To build from source, you will need these libraries installed:

  - [cjose](https://github.com/cisco/cjose)
  - [jansson](https://github.com/akheron/jansson)
  - pcre
  - OpenSSL

… as well as compiler toolchain.

This builds in-tree with the rest of the ATS plugins. Of special note, however,
are the first two libraries: cjose and jansson. These libraries are not
currently used anywhere else, so they may not be installed.

As of this writing, both libraries install a dynamic library and a static
archive. However, by default, the static archive is not compiled with Position
Independent Code. The build script will detect this and build a dynamic
dependency on these libraries, so they will have to be distributed with the
plugin.

If you would like to statically link them, you will need to ensure that they are
compiled with the `-fPIC` flag in their CFLAGs. If the archives have PIC, the
build scripts will automatically statically link them.
