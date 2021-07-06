Python URI Signer
==================

Given a configuration file and a URI, this python script will generate a signed URI according to the URI signing protocol outlined [here](https://tools.ietf.org/html/draft-ietf-cdni-uri-signing-16):

The script takes a config file and a uri as command line arguments. It picks one of the keys located in the json file at random
and embeds a valid JWT as a query string parameter into the uri and prints this new signed URI to standard out.

** Disclaimer **
Please note that this script is provided as a very simple example of how to implement a signer should not be considered production ready.

Requirements
------

[python-jose](https://pypi.org/project/python-jose/) library must be installed (pip install python-jose).

Config
------

The config file should be a JSON object that contains the following:

  - `iss`: A string representing the issuer of the token
  - `token_lifetime`: The lifetime of the token in seconds. Expiry of the token is calculated as now + token_lifetime
  - `aud`: A string representing the intended audience of the token.
  - `cdnistt`: Boolean value which if set to true uses cookie signed token transport, allowing the validator of the token to
    to issue subsequent tokens via set cookie headers.
  - `cdniets`: Must be set if using cdnistt. Provides means of setting Expiry Times when generating subsequent tokens. It denotes
    the number of seconds to be added to the time at which the JWT is verified that gives the value of the Expiry Time claim of the
    next signed JWT.
  - `cdnistd`: Integer value representing number of path segments that renewal token cookies should valid for. This is used when
     generating the path attribute of the cookies containing renewal tokens.
  - `keys`: A list of json objects, each one representing a key. Each key should have the following attributes:
      - `alg`: The Cryptographic algorithm to be used with the key.
      - `kid`: The key identifier
      - `kty`: The key type
      - `k`: The key itself

example_config.json can be used as a template for the configuration file.
