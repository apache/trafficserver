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
 * @file access_control.cc
 * @brief access control utilities
 */

#include <iostream>
#include <string>

#include "access_control.h"

size_t calcMessageDigest(const StringView hf, const char *secret, const char *message, size_t messageLen, char *buffer, size_t len);
const char *getSecretMap(const StringMap &map, const StringView &key, size_t &secretSize);

/* AccessToken ***************************************************************************************************** */

AccessToken::AccessToken(const StringMap &secretsMap, bool enableDebug) : _debug(enableDebug), _secretsMap(secretsMap) {}

AccessTokenStatus
AccessToken::validate(const StringView token, time_t time)
{
  if (token.empty()) {
    /* Empty token is likely not valid, so short-cut here. */
    return _state = INVALID_SYNTAX;
  }

  /* Parse and validate syntax */
  if (VALID != parse(token)) {
    return _state;
  }

  /* Validate field semantics and set defaults */
  if (VALID != validateSemantics()) {
    return _state;
  }

  /* Validate signature */
  if (VALID != validateSignature()) {
    return _state;
  }

  /* Now after we validated the signature, check timing */
  if (VALID != validateTiming(time)) {
    return _state;
  }

  /** @todo: validate scope eventually */

  return _state;
}

AccessTokenStatus
AccessToken::validateSemantics()
{
  /* Check for required or incompatible fields, set defaults */
  if (_subject.empty()) {
    ERROR_OUT("missing subject field, what are we signing and validating?");
    return _state = MISSING_REQUIRED_FIELD;
  }

  if (_expiration.empty()) {
    ERROR_OUT("missing expiration field, have to limit the life of the token");
    return _state = MISSING_REQUIRED_FIELD;
  }

  if (_keyId.empty()) {
    ERROR_OUT("missing keyId field, at least one key should be specified");
    return _state = MISSING_REQUIRED_FIELD;
  }

  if (_messageDigest.empty()) {
    ERROR_OUT("missing md field");
    return _state = MISSING_REQUIRED_FIELD;
  }

  /* Semantics checked and defaults set successfully */
  return _state;
}

AccessTokenStatus
AccessToken::validateSignature()
{
  /* Get secret needed for the signature */
  size_t secretLen   = 0;
  const char *secret = getSecretMap(_secretsMap, _keyId, secretLen);
  if (nullptr == secret || 0 == secretLen) {
    ERROR_OUT("failed to find the secret for key id: '" << _keyId << "'");
    return _state = INVALID_SECRET;
  }

  /* Calculate signature. */
  char computedMd[MAX_MSGDIGEST_BUFFER_SIZE];
  size_t computedMdLen = 0;
  computedMdLen = calcMessageDigest(_hashFunction, secret, _payload.data(), _payload.size(), computedMd, MAX_MSGDIGEST_BUFFER_SIZE);
  if (0 == computedMdLen) {
    ERROR_OUT("failed to calculate message digest");
    return _state = INVALID_SIGNATURE;
  }

  /* Convert MD from the package into binary before comparing */
  char tokenMd[MAX_MSGDIGEST_BUFFER_SIZE];
  memset(tokenMd, 0, MAX_MSGDIGEST_BUFFER_SIZE);
  size_t tokenMdLen = hexDecode(_messageDigest.data(), _messageDigest.size(), tokenMd, MAX_MSGDIGEST_BUFFER_SIZE);
  if (0 == tokenMdLen) {
    DEBUG_OUT("failed to hex-decode token md");
    return _state = INVALID_FIELD_VALUE;
  }
  DEBUG_OUT("token md=" << _messageDigest);

  /* Signature check first */
  if (!cryptoMessageDigestEqual((const char *)tokenMd, tokenMdLen, (const char *)computedMd, computedMdLen)) {
    ERROR_OUT("invalid signature");
    return _state = INVALID_SIGNATURE;
  }

  /* Valid signature (MD) */
  return _state;
}

AccessTokenStatus
AccessToken::validateTiming(time_t time)
{
  time_t t = 0;

  _validationTime = time; /* saved for debugging / troubleshooting */

  /* Validate and check not before timestamp */
  if (!_notBefore.empty()) {
    if (0 == (t = string2int(_notBefore))) {
      return _state = INVALID_FIELD_VALUE;
    } else {
      if (time <= t) {
        return _state = TOO_EARLY;
      }
    }
  }

  /* Validate and check expiration timestamp */
  if (!_expiration.empty()) {
    if (0 == (t = string2int(_expiration))) {
      return _state = INVALID_FIELD_VALUE;
    } else {
      if (time > t) {
        return _state = TOO_LATE;
      }
    }
  }

  /* "issued at" time-stamp is currently only for info, so check if the time-stamp is valid only */
  if (!_issuedAt.empty() && 0 == string2int(_issuedAt)) {
    return _state = INVALID_FIELD_VALUE;
  }

  return _state;
}

/* KvpAccessToken ************************************************************************************************** */

KvpAccessToken::KvpAccessToken(const KvpAccessTokenConfig &tokenConfig, const StringMap &secretsMap, bool enableDebug)
  : AccessToken(secretsMap, enableDebug), _tokenConfig(tokenConfig)
{
}

AccessTokenStatus
KvpAccessToken::parse(const StringView token)
{
  /* Initializing it here, clear the unused state, assume VALID and try to find problems */
  _state = VALID;
  _token = token;

  DEBUG_OUT("token:'" << _token << "'");

  size_t payloadSize = 0;
  size_t prev        = 0;
  size_t pos         = 0;
  do {
    /* Look for the next KVP */
    pos              = _token.find(_tokenConfig.pairDelimiter, prev);
    StringView kvp   = _token.substr(prev, pos - prev);
    size_t equalsign = kvp.find(_tokenConfig.kvDelimiter);
    if (kvp.npos == equalsign) {
      ERROR_OUT("invalid key-value-pair, missing key-value delimiter");
      return _state = INVALID_SYNTAX;
    }
    StringView key   = kvp.substr(0, equalsign);
    StringView value = equalsign != kvp.npos ? kvp.substr(equalsign + 1) : "";

    DEBUG_OUT("kvp:'" << kvp << "', key:'" << key << "', value:'" << value << "'");

    /* Initialize the corresponding member */
    payloadSize = prev;
    if (_tokenConfig.subjectName == key) {
      _subject = value;
    } else if (_tokenConfig.expirationName == key) {
      _expiration = value;
    } else if (_tokenConfig.notBeforeName == key) {
      _notBefore = value;
    } else if (_tokenConfig.issuedAtName == key) {
      _issuedAt = value;
    } else if (_tokenConfig.tokenIdName == key) {
      _tokenId = value;
    } else if (_tokenConfig.versionName == key) {
      _version = value;
    } else if (_tokenConfig.scopeName == key) {
      _scope = value;
    } else if (_tokenConfig.keyIdName == key) {
      _keyId = value;
    } else if (_tokenConfig.hashFunctionName == key) {
      _hashFunction = value;
    } else if (_tokenConfig.messageDigestName == key) {
      _messageDigest = value;
    } else {
      ERROR_OUT("failed to construct a valid access token");
      return _state = INVALID_FIELD;
    }

    prev = pos + _tokenConfig.kvDelimiter.size();
  } while (pos != token.npos);

  /* Now identify the pay-load which was signed */
  payloadSize += _tokenConfig.messageDigestName.size() + _tokenConfig.kvDelimiter.size();
  _payload = _token.substr(0, payloadSize);

  DEBUG_OUT("payload:'" << _payload << "'");

  /* successful parsing */
  return _state;
}

/* AccessTokenBuilder ********************************************************************************************** */

KvpAccessTokenBuilder::KvpAccessTokenBuilder(const KvpAccessTokenConfig &config, const StringMap &secretsMap)
  : _config(config), _secretsMap(secretsMap)
{
  cryptoMagicInit();
}

void
KvpAccessTokenBuilder::appendKeyValuePair(const StringView &key, const StringView value)
{
  _buffer.append(_buffer.empty() ? "" : _config.pairDelimiter);
  _buffer.append(key).append(_config.kvDelimiter).append(value);
}

void
KvpAccessTokenBuilder::addSubject(const StringView sub)
{
  appendKeyValuePair(_config.subjectName, sub);
}
void
KvpAccessTokenBuilder::addExpiration(time_t exp)
{
  appendKeyValuePair(_config.expirationName, std::to_string(exp));
}
void
KvpAccessTokenBuilder::addNotBefore(time_t nbf)
{
  appendKeyValuePair(_config.notBeforeName, std::to_string(nbf));
}
void
KvpAccessTokenBuilder::addIssuedAt(time_t iat)
{
  appendKeyValuePair(_config.issuedAtName, std::to_string(iat));
}
void
KvpAccessTokenBuilder::addTokenId(const StringView tid)
{
  appendKeyValuePair(_config.tokenIdName, tid);
}
void
KvpAccessTokenBuilder::addVersion(const StringView ver)
{
  appendKeyValuePair(_config.versionName, ver);
}
void
KvpAccessTokenBuilder::addScope(const StringView scope)
{
  appendKeyValuePair(_config.scopeName, scope);
}
void
KvpAccessTokenBuilder::sign(const StringView kid, const StringView hf)
{
  appendKeyValuePair(_config.keyIdName, kid);
  appendKeyValuePair(_config.hashFunctionName, hf);
  appendKeyValuePair(_config.messageDigestName, ""); /* add an empty message digest and append the actual digest later */

  char md[MAX_MSGDIGEST_BUFFER_SIZE];
  size_t secretLen   = 0;
  const char *secret = getSecretMap(_secretsMap, kid, secretLen);
  if (nullptr == secret || 0 == secretLen) {
    ERROR_OUT("failed to find the secret for kid='" << kid << "'");
    return;
  }

  size_t mdLen = calcMessageDigest(hf, secret, _buffer.data(), _buffer.size(), md, MAX_MSGDIGEST_BUFFER_SIZE);
  if (0 == mdLen) {
    DEBUG_OUT("failed to calculate message digest");
  } else {
    /* Hex-encode signature. */
    char mdHexLenMax = 2 * mdLen + 1;
    char mdHex[mdHexLenMax];
    size_t mdHexLen = hexEncode(md, mdLen, mdHex, mdHexLenMax);
    if (0 == mdHexLen) {
      DEBUG_OUT("failed to hex-encode new MD");
    } else {
      DEBUG_OUT(_config.messageDigestName << "=" << StringView(mdHex, mdHexLen) << " (" << (int)mdHexLen << ")");
      _buffer.append(mdHex, mdHexLen);
    }
  }
}

const char *
KvpAccessTokenBuilder::get()
{
  return _buffer.c_str();
}
/* Crypto related ********************************************************************************************** */

/* OpenSSL library hash function names */
#define LIBSSL_HASH_SHA256 "SHA256"
#define LIBSSL_HASH_SHA512 "SHA512"

/* encryption digest algorithm to openssl digest algorithm names. */
static std::map<String, String>
createStaticDigestAlgoMap()
{
  std::map<String, String> algos;
  algos[WDN_HASH_SHA256] = LIBSSL_HASH_SHA256;
  algos[WDN_HASH_SHA512] = LIBSSL_HASH_SHA512;
  return algos;
};

/**
 * A static map that maps well-defined name to openssl names.
 */
static const std::map<String, String> _digestAlgosMap = createStaticDigestAlgoMap();

/**
 * @brief Calculates message digest
 *
 * @param hf Hash Function (HF) [optional]
 * @param secret secret
 * @param message input message
 * @param messageLen input message lenght
 * @param buffer output buffer for storing the message digest
 * @param len output buffer length
 * @return number of characters actually written to the output buffer.
 */
size_t
calcMessageDigest(const StringView hf, const char *secret, const char *message, size_t messageLen, char *buffer, size_t len)
{
  if (hf.empty()) {
    return cryptoMessageDigestGet(LIBSSL_HASH_SHA256, message, messageLen, secret, strlen(secret), buffer, len);
  } else {
    std::map<String, String>::const_iterator it = _digestAlgosMap.find(String(hf.data(), hf.size()));
    if (_digestAlgosMap.end() == it) {
      AccessControlError("Unsupported digest name '%.*s'", (int)hf.size(), hf.data());
      return 0;
    }

    return cryptoMessageDigestGet(it->second.c_str(), message, messageLen, secret, strlen(secret), buffer, len);
  }
}

/**
 * @brief Get a secret from a map of secrets based on an index (i.e. KID)
 *
 * @param map string map containing secrets
 * @param key string containing the key (i.e. KID)
 * @return ptr to NULL-terminated C-string with the secret.
 */
const char *
getSecretMap(const StringMap &map, const StringView &key, size_t &secretSize)
{
  secretSize = 0;

  if (map.empty()) {
    DEBUG_OUT("secrets map is empty");
    return nullptr;
  }
  const char *result = nullptr;
  StringMap::const_iterator it;
  it = map.find(String(key));
  if (map.end() != it) {
    result     = it->second.c_str();
    secretSize = it->second.size();
#ifdef ACCESS_CONTROL_LOG_SECRETS
    DEBUG_OUT("secrets[" << key << "] = '" << result << "'");
#endif
  } else {
    DEBUG_OUT("secrets[" << key << "] does not exist");
  }
  return result;
}

/**
 * Access token validation status converted string representation.
 */
const char *
accessTokenStatusToString(const AccessTokenStatus &state)
{
  const char *s = nullptr;
  switch (state) {
  case VALID:
    s = "VALID";
    break;
  case UNUSED:
    s = "UNUSED";
    break;
  case INVALID_SYNTAX:
    s = "PARSING_FAILURE";
    break;
  case MISSING_REQUIRED_FIELD:
    s = "MISSING_REQUIRED_FIELD";
    break;
  case INVALID_FIELD:
    s = "UNEXPECTED_FIELD";
    break;
  case INVALID_FIELD_VALUE:
    s = "INVALID_FIELD_VALUE";
    break;
  case INVALID_VERSION:
    s = "UNSUPORTED_VERSION";
    break;
  case INVALID_SECRET:
    s = "NO_SECRET_SPECIFIED";
    break;
  case INVALID_SIGNATURE:
    s = "INVALID_SIGNATURE";
    break;
  case TOO_EARLY:
    s = "TOO_EARLY";
    break;
  case TOO_LATE:
    s = "TOO_LATE";
    break;
  case INVALID_SCOPE:
    s = "INVALID_SCOPE";
    break;
  case OUT_OF_SCOPE:
    s = "OUT_OF_SCOPE";
    break;
  case INVALID_KEYID:
    s = "INVALID_KEYID";
    break;
  case INVALID_HASH_FUNCTION:
    s = "UNSUPORTED_HASH_FUNCTION";
    break;
  default:
    s = "";
    break;
  }
  return s;
}

/* Debug dump of the token */
std::ostream &
operator<<(std::ostream &os, const AccessToken &token)
{
  os << "=== debug ==============================" << std::endl;
  os << "(d) token     : '" << token._token << "'" << std::endl;
  os << "(d) state     : " << accessTokenStatusToString(token._state) << std::endl;
  os << "(d) checked-at: " << token._validationTime << std::endl;
  os << "=== claims =============================" << std::endl;
  os << "(r) subject   : '" << token._subject << "'" << std::endl;
  os << "--- timing -----------------------------" << std::endl;
  os << "(o) expiration: '" << token._expiration << "' (" << token.getExpiration() << ")" << std::endl;
  os << "(o) not-before: '" << token._notBefore << "' (" << token.getNotBefore() << ")" << std::endl;
  os << "(o) issued-at : '" << token._issuedAt << "' (" << token.getIssuedAt() << ")" << std::endl;
  os << "----------------------------------------" << std::endl;
  os << "(o) token-id  : '" << token._tokenId << "'" << std::endl;
  os << "(o) version   : '" << token._version << "'" << std::endl;
  os << "(o) scope     : '" << token._scope << "'" << std::endl;
  os << "--- signature related ------------------" << std::endl;
  os << "(o) key-id    : '" << token._keyId << "'" << std::endl;
  os << "(o) hash-func : '" << token._hashFunction << "'" << std::endl;
  os << "(r) digest    : '" << token._messageDigest << "'" << std::endl;

  return os;
}
