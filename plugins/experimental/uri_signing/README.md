URI Signing Plugin
==================

This remap plugin implements the draft URI Signing protocol documented [here](https://tools.ietf.org/html/draft-ietf-cdni-uri-signing-16):

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

### Keys

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

### Auth Directives

It's occasionally useful to allow un-signed access to specific paths. To
that end, the `auth_directives` parameter is supported. It can be used
like this:

    {
      "Kabletown URI Authority": {
        "renewal_kid": "Second Key",
        "auth_directives": [
          { auth: "allow", uri: "uri-regex:.*crossdomain.xml" },
          { auth: "deny",  uri: "uri-regex:https?://[^/]*/public/secret.xml.*" },
          { auth: "allow", uri: "uri-regex:https?://[^/]*/public/.*" },
          { auth: "allow", uri: "uri-regex:.*favicon.ico" }
        ]
        "keys": [
          ⋮
        ]
    }

Each of the `auth_directives` will be evaluated in order for each url
that does not have a valid token. If it matches an allowed path before
it matches a denied one, it will be served anyway. If it matches no
`auth_directives`, it will not be served.

It's worth noting that multiple issuers can provide `auth_directives`.
Each issuer will be processed in order and any issuer can provide access to
a path.

### More Configuration Options

**Strip Token**
When the strip_token parameter is set to true, the plugin removes the
token from both the url that is sent upstream to the origin and the url that
is used as the cache key. The strip_token parameter defaults to false and should
be set by only one issuer.
**ID**
The id field takes a string indicating the identification of the entity processing the request.
This is used in aud claim checks to ensure that the receiver is the intended audience of a
tokenized request. The id parameter can only be set by one issuer.

Example:

    {
      "Kabletown URI Authority": {
        "renewal_kid": "Second Key",
        "strip_token" : true,
        "id" : "mycdn",
        "auth_directives": [
          ⋮
        ]
        "keys": [
          ⋮
        ]
    }

Usage
-----

The URI signing plugin will block all requests that do not bear a valid JWT, as
defined by the URI Signing protocol. Clients that do not present a valid JWT
will receive a 403 Forbidden response, instead of receiving content.

Tokens will be found in either of these places:

  - A query parameter named `URISigningPackage`. The value must be the JWT.
  - A path parameter named `URISigningPackage`. The value must be the JWT.
  - A cookie named `URISigningPackage`. The value of the cookie must be the JWT.

### Supported Claims

The following claims are understood:

  - `iss`: Must be present. The issuer is used to locate the key for verification.
  - `sub`: May be present, but is not validated.
  - `exp`: Expired tokens are not valid.
  - `nbf`: Tokens processed before this time are not valid.
  - `aud`: Token aud claim strings must match the configured id to be considered valid.
  - `iat`: May be present, but is not validated.
  - `cdniv`: Must be missing or 1.
  - `cdniuc`: Validated last, after key verificationD. **Only `regex` is supported!**
  - `cdniets`: If cdnistt is 1, this must be present and non-zero.
  - `cdnistt`: If present, must be 1.
  - `cdnistd`: If present, must be 0.

### Unsupported Claims

These claims are not supported. If they are present, the token will not validate:

  - `jti`
  - `cdnicrit`
  - `cdniip`

In addition, the `cdniuc` container of `hash` is
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

Note that the default prefix value for cjose is /usr/local. Ensure this is visible to
any executables that are being run using this library.

As of this writing, both libraries install a dynamic library and a static
archive. However, by default, the static archive is not compiled with Position
Independent Code. The build script will detect this and build a dynamic
dependency on these libraries, so they will have to be distributed with the
plugin.

If you would like to statically link them, you will need to ensure that they are
compiled with the `-fPIC` flag in their CFLAGs. If the archives have PIC, the
build scripts will automatically statically link them.

Here are some sample commands for building jansson, cjose and trafficserver
locally using static linking.  This assumes all source is under ${HOME}/git.

### Sample

If using local jansson:

    cd ${HOME}/git
    git clone https://github.com/akheron/jansson.git
    cd jansson
    autoreconf -i
    ./configure --disable-shared CC="gcc -fpic"
    make -j`nproc`

    # Needed for ATS configure
    ln -s src/.libs lib
    ln -s src include

If using local cjose:

    cd ${HOME}/git
    git clone https://github.com/cisco/cjose.git
    cd cjose
    autoreconf -i
    ./configure --with-jansson=${HOME}/git/jansson --disable-shared CC="gcc -fpic"
    make -j`nproc`

    # Needed for ATS configure
    ln -s src/.libs lib

ATS:

    cd ${HOME}/git/
    git clone https://github.com/apache/trafficserver.git
    cd trafficserver
		autoreconf -i
    ./configure --enable-experimental-plugins --with-jansson=${HOME}/git/jansson --with-cjose=${HOME}/git/cjose
    make -j`nproc`
