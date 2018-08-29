/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

/**
 * @file access_control.h
 * @brief access control include file
 */

#pragma once

#include <ctime>
#include <map>
#include <iostream>

#include "common.h"
#include "utils.h"

/* Quick and dirty development only output, @todo will do something more useful later so we can use it in production debugging */
#define DEBUG_OUTPUT_ENABLED false

#define DEBUG_PRINT(x)          \
  do {                          \
    if (DEBUG_OUTPUT_ENABLED) { \
      std::cerr << x;           \
    }                           \
  } while (0)

#define DEBUG_START(x)                                 \
  do {                                                 \
    if (DEBUG_OUTPUT_ENABLED) {                        \
      std::cerr << __FILE__ << ":" << __LINE__ << " "; \
    }                                                  \
  } while (0)

#define DEBUG_END(x)            \
  do {                          \
    if (DEBUG_OUTPUT_ENABLED) { \
      std::cerr << std::endl;   \
    }                           \
  } while (0)

#define DEBUG_OUT(x) \
  do {               \
    DEBUG_START(x);  \
    DEBUG_PRINT(x);  \
    DEBUG_END(x);    \
  } while (0);

#define ERROR_OUT DEBUG_OUT

/**
 *  The names of the first version of access token, have it here so we can make it reconfigurable later.
 */
struct KvpAccessTokenConfig {
  const String subjectName     = "sub";
  StringView expirationName    = "exp";
  StringView notBeforeName     = "nbf";
  StringView issuedAtName      = "iat";
  StringView tokenIdName       = "tid";
  StringView versionName       = "ver";
  StringView scopeName         = "scope";
  StringView keyIdName         = "kid";
  StringView hashFunctionName  = "st";
  StringView messageDigestName = "md";

  String pairDelimiter = "&";
  String kvDeliiter    = "=";
};

/**
 * Access token validation status.
 */
enum AccessTokenStatus {
  VALID,
  UNUSED,
  INVALID_SYNTAX,
  INVALID_FIELD,
  INVALID_FIELD_VALUE,
  MISSING_REQUIRED_FIELD,
  INVALID_VERSION,
  INVALID_HASH_FUNCTION,
  INVALID_KEYID,
  INVALID_SECRET,
  INVALID_SIGNATURE,
  INVALID_SCOPE,
  OUT_OF_SCOPE,
  TOO_EARLY,
  TOO_LATE,
  MAX,
};

const char *accessTokenStatusToString(const AccessTokenStatus &state);

/**
 *  Base Access Token class / interface + some basic implementations.
 */
class AccessToken
{
  friend std::ostream &operator<<(std::ostream &os, const AccessToken &token);

public:
  AccessToken(const StringMap &secretsMap, bool enableDebug = false);
  virtual ~AccessToken() {}
  StringView
  getSubject() const
  {
    return _subject;
  }

  time_t
  getExpiration() const
  {
    return string2int(_expiration);
  }

  time_t
  getNotBefore() const
  {
    return string2int(_notBefore);
  }

  time_t
  getIssuedAt() const
  {
    return string2int(_issuedAt);
  }

  StringView
  getTokenId() const
  {
    return _tokenId;
  }

  StringView
  getVersion() const
  {
    return _version;
  }

  StringView
  getScope() const
  {
    return _scope;
  }

  StringView
  getKeyId() const
  {
    return _keyId;
  }

  StringView
  getHashFunction() const
  {
    return _hashFunction;
  }

  AccessTokenStatus
  getState()
  {
    return _state;
  }

  AccessTokenStatus validate(const StringView token, time_t time);
  virtual AccessTokenStatus parse(const StringView token) = 0;

protected:
  AccessTokenStatus validateSemantics();
  AccessTokenStatus validateSignature();
  AccessTokenStatus validateTiming(time_t time);

  /* Initial setup members */
  bool _debug = false;               /** @brief collect and print more debugging info */
  const StringMap &_secretsMap;      /** @brief map with secret for signing the package*/
  AccessTokenStatus _state = UNUSED; /** @brief token state */
  time_t _validationTime   = 0;      /** @brief validation time used for debugging */

  /* Helper members */
  StringView _token   = ""; /** @brief whole token */
  StringView _payload = ""; /** @brief payload signed by the signature */

  /* Fields extracted from the token string */
  StringView _subject    = ""; /** @brief subject - this is what we are signing and validating, required */
  StringView _expiration = ""; /** @brief expiration time-stamp, not required */
  StringView _notBefore  = ""; /** @brief not before time-stamp, not required */
  StringView _issuedAt   = ""; /** @brief time-stamp when token was issued, not required */
  StringView _tokenId    = ""; /** @brief unique token id for debugging and tracking, not required */
  StringView _version    = ""; /** @brief version, not required, still @todo */
  StringView _scope      = ""; /** @brief scope of subject, not required, still @todo */

  /** Signature, extracted from the token string */
  StringView _keyId         = ""; /** @brief the key in the secrets map to be used to calculate the digest */
  StringView _hashFunction  = ""; /** @brief name of the hash function to be used for the digest */
  StringView _messageDigest = ""; /** @brief the message digest that signs the token */
};

/**
 * Key-value-pair access token
 */
class KvpAccessToken : public AccessToken
{
public:
  KvpAccessToken(const KvpAccessTokenConfig &tokenConfig, const StringMap &secretsMap, bool enableDebug = false);
  AccessTokenStatus parse(const StringView token);

protected:
  const KvpAccessTokenConfig &_tokenConfig; /** @brief description of keys' names and delimiters */
};

class KvpAccessTokenBuilder
{
public:
  KvpAccessTokenBuilder(const KvpAccessTokenConfig &config, const StringMap &secretsMap);

  void appendKeyValuePair(const StringView &key, const StringView value);
  void addSubject(const StringView sub);
  void addExpiration(time_t exp);
  void addNotBefore(time_t nbf);
  void addIssuedAt(time_t iat);
  void addTokenId(const StringView tid);
  void addVersion(const StringView ver);
  void addScope(const StringView scope);
  void sign(const StringView kid, const StringView hf);
  const char *get();

private:
  const KvpAccessTokenConfig &_config;
  String _buffer;

  const StringMap &_secretsMap; /** @brief map with secret for signing the package*/
};

/**
 * Instantiate various types of Access Tokens from a single place.
 * @todo see how it goes when adding new token kinds and redesign / re-implement later.
 */
class AccessTokenFactory
{
public:
  enum TokenType {
    Unknown,
    KeyValuePair,
  };

  AccessTokenFactory(const KvpAccessTokenConfig &tokenConfig, const StringMap &secretsMap, bool enableDebug)
    : _kvpAccessTokenConfig(tokenConfig), _secretMap(secretsMap), _enableDebug(enableDebug)
  {
    cryptoMagicInit();
    _desiredType = KeyValuePair;
  }

  AccessToken *
  getAccessToken()
  {
    switch (_desiredType) {
    case KeyValuePair: {
      return new KvpAccessToken(_kvpAccessTokenConfig, _secretMap, _enableDebug);
      break;
    }
    default: {
      break;
    }
    }
    return nullptr;
  }

private:
  TokenType _desiredType = Unknown; /* Remember for each (only one) token type the factory was initialized */
  const KvpAccessTokenConfig &_kvpAccessTokenConfig;
  const StringMap &_secretMap;
  bool _enableDebug = false;

  AccessTokenFactory() = delete;
};

/* Define user friendly names for supported hash functions and cryptographic signature schemes */
#define WDN_HASH_SHA256 "HMAC-SHA-256"
#define WDN_HASH_SHA512 "HMAC-SHA-512"
#define WDN_RSA_PSS "RSA_PSS"
