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
 * @file plugin.cc
 * @brief traffic server plugin entry points.
 */

#include <ctime> /* strftime */

#include "common.h"         /* Common definitions */
#include "config.h"         /* AccessControlConfig */
#include "access_control.h" /* AccessToken */
#include "ts/remap.h"       /* TSRemapInterface, TSRemapStatus, apiInfo */
#include "ts/ts.h"          /* ATS API */
#include "utils.h"          /* cryptoBase64Decode.* functions */
#include "headers.h"        /* getHeader, setHeader, removeHeader */

static const std::string_view UNKNOWN{"unknown"};

static const char *
getEventName(TSEvent event)
{
  switch (event) {
  case TS_EVENT_HTTP_CONTINUE:
    return "TS_EVENT_HTTP_CONTINUE";
  case TS_EVENT_HTTP_ERROR:
    return "TS_EVENT_HTTP_ERROR";
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    return "TS_EVENT_HTTP_READ_REQUEST_HDR";
  case TS_EVENT_HTTP_OS_DNS:
    return "TS_EVENT_HTTP_OS_DNS";
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    return "TS_EVENT_HTTP_SEND_REQUEST_HDR";
  case TS_EVENT_HTTP_READ_CACHE_HDR:
    return "TS_EVENT_HTTP_READ_CACHE_HDR";
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    return "TS_EVENT_HTTP_READ_RESPONSE_HDR";
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    return "TS_EVENT_HTTP_SEND_RESPONSE_HDR";
  case TS_EVENT_HTTP_REQUEST_TRANSFORM:
    return "TS_EVENT_HTTP_REQUEST_TRANSFORM";
  case TS_EVENT_HTTP_RESPONSE_TRANSFORM:
    return "TS_EVENT_HTTP_RESPONSE_TRANSFORM";
  case TS_EVENT_HTTP_SELECT_ALT:
    return "TS_EVENT_HTTP_SELECT_ALT";
  case TS_EVENT_HTTP_TXN_START:
    return "TS_EVENT_HTTP_TXN_START";
  case TS_EVENT_HTTP_TXN_CLOSE:
    return "TS_EVENT_HTTP_TXN_CLOSE";
  case TS_EVENT_HTTP_SSN_START:
    return "TS_EVENT_HTTP_SSN_START";
  case TS_EVENT_HTTP_SSN_CLOSE:
    return "TS_EVENT_HTTP_SSN_CLOSE";
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    return "TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE";
  case TS_EVENT_HTTP_PRE_REMAP:
    return "TS_EVENT_HTTP_PRE_REMAP";
  case TS_EVENT_HTTP_POST_REMAP:
    return "TS_EVENT_HTTP_POST_REMAP";
  default:
    return "UNHANDLED";
  }
  return "UNHANDLED";
}

/**
 * @brief Plugin transaction data.
 */
class AccessControlTxnData
{
public:
  AccessControlTxnData(AccessControlConfig *config) : _config(config) {}
  const AccessControlConfig *_config;      /** @brief pointer to the plugin config */
  String _subject                = "";     /** @brief subject for debugging purposes */
  AccessTokenStatus _vaState     = UNUSED; /** @brief VA access control token validation status */
  AccessTokenStatus _originState = UNUSED; /** @brief Origin access control token validation status */
};

/**
 * @brief Plugin initialization.
 * @param apiInfo remap interface info pointer
 * @param errBuf error message buffer
 * @param errBufSize error message buffer size
 * @return always TS_SUCCESS.
 */
TSReturnCode
TSRemapInit(TSRemapInterface *apiInfo, char *errBuf, int erroBufSize)
{
  return TS_SUCCESS;
}

/**
 * @brief Plugin new instance entry point.
 *
 * Processes the configuration and initializes the plugin instance.
 * @param argc plugin arguments number
 * @param argv plugin arguments
 * @param instance new plugin instance pointer (initialized in this function)
 * @param errBuf error message buffer
 * @param errBufSize error message buffer size
 * @return TS_SUCCES if success or TS_ERROR if failure
 */
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **instance, char *errBuf, int errBufSize)
{
  AccessControlConfig *config = new AccessControlConfig();
  if (nullptr != config && config->init(argc, argv)) {
    *instance = config;
  } else {
    AccessControlDebug("failed to initialize the " PLUGIN_NAME " plugin");
    *instance = nullptr;
    delete config;
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

/**
 * @brief Plugin instance deletion clean-up entry point.
 * @param plugin instance pointer.
 */
void
TSRemapDeleteInstance(void *instance)
{
  AccessControlConfig *config = static_cast<AccessControlConfig *>(instance);
  delete config;
}

/**
 * @brief A mapping between various failures and HTTP status code and message to be returned to the UA.
 * @param state Access Token validation status
 * @param config pointer to the plugin configuration to get the desired response for each failure.
 * @return HTTP status
 */
static TSHttpStatus
accessTokenStateToHttpStatus(AccessTokenStatus state, AccessControlConfig *config)
{
  TSHttpStatus httpStatus = TS_HTTP_STATUS_NONE;
  const char *message     = "VALID";
  switch (state) {
  case VALID:
    break;
  case INVALID_SIGNATURE:
    httpStatus = config->_invalidSignature;
    message    = "invalid signature";
    break;
  case UNUSED:
    httpStatus = config->_internalError;
    message    = "uninitialized token";
    break;
  case INVALID_SECRET:
    httpStatus = config->_internalError;
    message    = "failed to find secrets";
    break;
  case INVALID_SYNTAX:
  case MISSING_REQUIRED_FIELD:
  case INVALID_FIELD:
  case INVALID_FIELD_VALUE:
  case INVALID_VERSION:
  case INVALID_HASH_FUNCTION:
  case INVALID_KEYID:
    httpStatus = config->_invalidSyntax;
    message    = "invalid syntax";
    break;
  case INVALID_SCOPE:
  case OUT_OF_SCOPE:
    httpStatus = config->_invalidScope;
    message    = "invalid scope";
    break;
  case TOO_EARLY:
  case TOO_LATE:
    httpStatus = config->_invalidTiming;
    message    = "invalid timing ";
    break;
  default:
    /* Validation failed. */
    httpStatus = config->_invalidRequest;
    message    = "unknown error";
    break;
  }
  AccessControlDebug("token validation: %s", message);

  return httpStatus;
}

/**
 * @brief a quick utility function to trim leading spaces.
 */
static void
ltrim(String &target)
{
  String::size_type p(target.find_first_not_of(' '));

  if (p != target.npos) {
    target.erase(0, p);
  }
}

/**
 * @brief a quick utility function to get next duplicate header.
 */
static TSMLoc
nextDuplicate(TSMBuffer buffer, TSMLoc hdr, TSMLoc field)
{
  TSMLoc next = TSMimeHdrFieldNextDup(buffer, hdr, field);
  TSHandleMLocRelease(buffer, hdr, field);
  return next;
}

/**
 * @brief Append cookies by following the rules specified in the cookies config object.
 * @param config cookies-related configuration containing information about which cookies need to be appended to the key.
 * @note Add the cookies to "hier-part" (RFC 3986), always sort them in the cache key.
 */
bool
getCookieByName(TSHttpTxn txn, TSMBuffer buf, TSMLoc hdrs, const String &cookieName, String &cookieValue)
{
  TSMLoc field;

  for (field = TSMimeHdrFieldFind(buf, hdrs, TS_MIME_FIELD_COOKIE, TS_MIME_LEN_COOKIE); field != TS_NULL_MLOC;
       field = ::nextDuplicate(buf, hdrs, field)) {
    int count = TSMimeHdrFieldValuesCount(buf, hdrs, field);

    for (int i = 0; i < count; ++i) {
      const char *val;
      int len;

      val = TSMimeHdrFieldValueStringGet(buf, hdrs, field, i, &len);
      if (val == nullptr || len == 0) {
        continue;
      }

      String cookie;
      std::istringstream istr(String(val, len));

      while (std::getline(istr, cookie, ';')) {
        ::ltrim(cookie); // Trim leading spaces.

        String::size_type pos(cookie.find_first_of('='));
        String name(cookie.substr(0, pos == String::npos ? cookie.size() : pos));

        AccessControlDebug("cookie name: %s", name.c_str());

        if (0 == cookieName.compare(name)) {
          cookieValue.assign(cookie.substr(pos == String::npos ? pos : pos + 1));
          return true;
        }
      }
    }
  }
  return false;
}

/**
 * @brief Handle token validation failures.
 * @param txn transaction handle
 * @param data transaction data
 * @param reject true - reject requests if configured, false - don't reject
 * @param httpStatus HTTP status
 * @param status Access Token validation status.
 */
static TSRemapStatus
handleInvalidToken(TSHttpTxn txnp, AccessControlTxnData *data, bool reject, const TSHttpStatus httpStatus, AccessTokenStatus status)
{
  TSRemapStatus resultStatus = TSREMAP_NO_REMAP;
  if (reject) {
    TSHttpTxnStatusSet(txnp, httpStatus);
    resultStatus = TSREMAP_DID_REMAP;
  } else {
    data->_vaState = status;
  }
  TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_CACHE_HTTP, 0);

  return resultStatus;
}

/**
 * @brief Formats the time stamp into expires cookie field format
 * @param expires Unix Time
 * @return String containing the date in the appropriate format for the Expires cookie attribute.
 */
String
getCookieExpiresTime(time_t expires)
{
  struct tm tm;
  char dateTime[1024];
  size_t dateTimeLen = 1024;

  size_t len = strftime(dateTime, dateTimeLen, "%a, %d %b %Y %H:%M:%S GMT", gmtime_r(&expires, &tm));
  return String(dateTime, len);
}

/**
 * @brief Callback function that handles cache lookup complete state where the access token is checked before serving from cache.
 *
 * If cache-miss or cache-skip don't validate - request will be forwarded to the origin and will be validated anyway.
 * If cache-hit or cache-hit-stale - validate access token and if validation fails force a cache-miss so request will be forwarded
 * to origin and validated.
 *
 * @param contp continuation associated with this function.
 * @param event corresponding event triggered at different hooks.
 * @param edata HTTP transaction structures (access control plugin config).
 * @return always 0
 */
int
contHandleAccessControl(const TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp                    = static_cast<TSHttpTxn>(edata);
  AccessControlTxnData *data        = static_cast<AccessControlTxnData *>(TSContDataGet(contp));
  const AccessControlConfig *config = data->_config;
  TSEvent resultEvent               = TS_EVENT_HTTP_CONTINUE;

  AccessControlDebug("event: '%s'", getEventName(event));

  switch (event) {
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: {
    if (VALID != data->_vaState && !config->_respTokenHeaderName.empty() && !config->_cookieName.empty()) {
      /* Set the cookie only if
       * - the initial client cookie validation failed (missing or invalid cookie)
       * - we expect a new access token from the origin
       * - provided token from the origin is valid
       * - and we know the name of the cookie to do set-cookie */

      TSMBuffer clientRespBufp;
      TSMLoc clientRespHdrLoc;
      if (TS_SUCCESS == TSHttpTxnClientRespGet(txnp, &clientRespBufp, &clientRespHdrLoc)) {
        TSMBuffer serverRespBufp;
        TSMLoc serverRespHdrLoc;
        if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &serverRespBufp, &serverRespHdrLoc)) {
          AccessControlDebug("got the response now create the cookie");

          static const size_t MAX_HEADER_LEN = 4096;

          int tokenHdrValueLen = MAX_HEADER_LEN;
          char tokenHdrValue[MAX_HEADER_LEN];

          getHeader(serverRespBufp, serverRespHdrLoc, config->_respTokenHeaderName.c_str(), config->_respTokenHeaderName.size(),
                    tokenHdrValue, &tokenHdrValueLen);

          if (0 < tokenHdrValueLen) {
            AccessControlDebug("origin response access token '%.*s'", tokenHdrValueLen, tokenHdrValue);

            AccessToken *token = config->_tokenFactory->getAccessToken();
            if (nullptr != token &&
                VALID == (data->_originState = token->validate(StringView(tokenHdrValue, tokenHdrValueLen), time(nullptr)))) {
              /*
               * From RFC 6265 "HTTP State Management Mechanism":
               * To maximize compatibility with user agents, servers that wish to
               * store arbitrary data in a cookie-value SHOULD encode that data, for
               * example, using Base64 [RFC4648].
               */
              int b64TokenHdrValueLen = cryptoBase64EncodedSize(tokenHdrValueLen);
              char b64TokenHdrValue[b64TokenHdrValueLen];
              size_t b64CookieLen =
                cryptoModifiedBase64Encode(tokenHdrValue, tokenHdrValueLen, b64TokenHdrValue, b64TokenHdrValueLen);

              String cookieValue;
              cookieValue.append(config->_cookieName).append("=").append(b64TokenHdrValue, b64CookieLen).append("; ");

              /** Currently Access Token implementation requires expiration to be set but the following is still a good
               * consideration. Set the cookie Expires field to the token expiration field set by the origin if the time specified
               * is invalid or not specified then don't set Expires attribute.
               * @todo TBD may be adding a default / overriding Expires attribute configured by parameter would make sense ? */
              time_t t = token->getExpiration();
              if (0 != t) {
                cookieValue.append("Expires=").append(getCookieExpiresTime(t)).append("; ");
              }

              /* Secure   - instructs the UA to include the cookie in an HTTP request only if the request is transmitted over
               *            a secure channel, typically HTTP over Transport Layer Security (TLS)
               * HttpOnly - instructs the UA to omit the cookie when providing access to cookies via "non-HTTP" APIs such as a web
               *            browser API that exposes cookies to scripts */
              cookieValue.append("path=/; Secure; HttpOnly");

              AccessControlDebug("%.*s: %s", TS_MIME_LEN_SET_COOKIE, TS_MIME_FIELD_SET_COOKIE, cookieValue.c_str());
              setHeader(clientRespBufp, clientRespHdrLoc, TS_MIME_FIELD_SET_COOKIE, TS_MIME_LEN_SET_COOKIE, cookieValue.c_str(),
                        cookieValue.size(), /* duplicateOk = */ true);

              delete token;
            } else {
              AccessControlDebug("failed to construct a valid origin access token, did not set-cookie with it");
              /* Don't set any cookie, fail the request here returning appropriate status code and body.*/
              TSHttpTxnStatusSet(txnp, config->_invalidOriginResponse);
              static const char *body = "Unexpected Response From the Origin Server\n";
              char *buf               = static_cast<char *>(TSmalloc(strlen(body) + 1));
              sprintf(buf, "%s", body);
              TSHttpTxnErrorBodySet(txnp, buf, strlen(buf), nullptr);

              resultEvent = TS_EVENT_HTTP_ERROR;
              break;
            }
          } else {
            AccessControlDebug("no access token response header found");
          }

          /* Remove the origin response access token header. */
          int numberOfFields = removeHeader(clientRespBufp, clientRespHdrLoc, config->_respTokenHeaderName.c_str(),
                                            config->_respTokenHeaderName.size());
          AccessControlDebug("removed %d %s client response header(s)", numberOfFields, config->_respTokenHeaderName.c_str());

          TSHandleMLocRelease(serverRespBufp, TS_NULL_MLOC, serverRespHdrLoc);
        } else {
          int len;
          char *url = TSHttpTxnEffectiveUrlStringGet(txnp, &len);
          AccessControlError("failed to retrieve server response header for request url:%.*s",
                             (len ? len : static_cast<int>(UNKNOWN.size())), (url ? url : UNKNOWN.data()));
        }

        TSHandleMLocRelease(clientRespBufp, TS_NULL_MLOC, clientRespHdrLoc);
      } else {
        int len;
        char *url = TSHttpTxnEffectiveUrlStringGet(txnp, &len);
        AccessControlError("failed to retrieve client response header for request url:%.*s",
                           (len ? len : static_cast<int>(UNKNOWN.size())), (url ? url : UNKNOWN.data()));
      }
    }
  } break;

  case TS_EVENT_HTTP_TXN_CLOSE: {
    if (!config->_extrValidationHdrName.empty()) {
      TSMBuffer clientRespBufp;
      TSMLoc clientRespHdrLoc;

      /* Add some debugging / logging to the client request so it can be extracted through headers */
      if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &clientRespBufp, &clientRespHdrLoc)) {
        String statusHeader;
        StringView vaState(accessTokenStatusToString(data->_vaState));
        StringView originState(accessTokenStatusToString(data->_originState));

        /* UC_ = UA Cookie, to store the token validation status when extracted from HTTP cookie */
        if (!vaState.empty()) {
          statusHeader.append("UC_").append(vaState);
        }
        /* OH_ = origin response header, to store the token validation status when extracted from origin response header. */
        if (!originState.empty()) {
          statusHeader.append(vaState.empty() ? "" : ",");
          statusHeader.append("OH_").append(originState);
        }
        AccessControlDebug("adding header %s: '%s'", config->_extrValidationHdrName.c_str(), statusHeader.c_str());
        setHeader(clientRespBufp, clientRespHdrLoc, config->_extrValidationHdrName.c_str(), config->_extrValidationHdrName.size(),
                  statusHeader.c_str(), statusHeader.length());

      } else {
        AccessControlError("failed to retrieve client response header");
      }
    }

    /* Destroy the txn continuation and its data */
    delete data;
    TSContDestroy(contp);
  } break;
  default:
    break;
  }

  TSHttpTxnReenable(txnp, resultEvent);
  return 0;
}

/**
 * @brief Enforces access control, currently supports access token from a cookie.
 *
 * @param instance plugin instance pointer
 * @param txn transaction handle
 * @param rri remap request info pointer
 * @param config pointer to the plugin configuration
 * @return TSREMAP_NO_REMAP (access validation = success)
 * TSREMAP_DID_REMAP (access validation = failure and rejection of failed requests is configured)
 */
TSRemapStatus
enforceAccessControl(TSHttpTxn txnp, TSRemapRequestInfo *rri, AccessControlConfig *config)
{
  if (config->_cookieName.empty()) {
    /* For now only checking a cookie is supported and if its name is unknown (checking cookie disabled) then do nothing. */
    return TSREMAP_NO_REMAP;
  }

  TSRemapStatus remapStatus = TSREMAP_NO_REMAP;

  /* Create txn data and register hooks */
  AccessControlTxnData *data = new AccessControlTxnData(config);
  TSCont cont                = TSContCreate(contHandleAccessControl, TSMutexCreate());
  TSContDataSet(cont, static_cast<void *>(data));
  TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, cont);

  /* Validate the token */
  bool reject = config->_rejectRequestsWithInvalidTokens;
  String cookie;
  bool found = getCookieByName(txnp, rri->requestBufp, rri->requestHdrp, config->_cookieName, cookie);
  if (found) {
    AccessControlDebug("%s cookie: '%s'", config->_cookieName.c_str(), cookie.c_str());

    /*
     * From RFC 6265 "HTTP State Management Mechanism":
     * To maximize compatibility with user agents, servers that wish to
     * store arbitrary data in a cookie-value SHOULD encode that data, for
     * example, using Base64 [RFC4648].
     */
    size_t decodedCookieBufferSize = cryptoBase64DecodeSize(cookie.c_str(), cookie.size());
    char decodedCookie[decodedCookieBufferSize];
    size_t decryptedCookieSize = cryptoModifiedBase64Decode(cookie.c_str(), cookie.size(), decodedCookie, decodedCookieBufferSize);
    if (0 < decryptedCookieSize) {
      AccessToken *token = config->_tokenFactory->getAccessToken();
      if (nullptr != token) {
        data->_vaState = token->validate(StringView(decodedCookie, decryptedCookieSize), time(nullptr));
        if (VALID != data->_vaState) {
          remapStatus =
            handleInvalidToken(txnp, data, reject, accessTokenStateToHttpStatus(data->_vaState, config), data->_vaState);
        } else {
          /* Valid token, if configured extract the token subject to a header,
           * only if we can trust it - token is valid to prevent using it by mistake */
          if (!config->_extrSubHdrName.empty()) {
            String sub(token->getSubject());
            setHeader(rri->requestBufp, rri->requestHdrp, config->_extrSubHdrName.c_str(), config->_extrSubHdrName.size(),
                      sub.c_str(), sub.size());
          }
        }
        /* If configure extract the UA token id into a header likely for debugging,
         * extract it even if token validation fails and we don't trust it */
        if (!config->_extrTokenIdHdrName.empty()) {
          String tokeId(token->getTokenId());
          setHeader(rri->requestBufp, rri->requestHdrp, config->_extrTokenIdHdrName.c_str(), config->_extrTokenIdHdrName.size(),
                    tokeId.c_str(), tokeId.size());
        }
        delete token;
      } else {
        AccessControlDebug("failed to construct access token");
        remapStatus = handleInvalidToken(txnp, data, reject, config->_internalError, UNUSED);
      }
    } else {
      AccessControlDebug("failed to decode cookie value");
      remapStatus = handleInvalidToken(txnp, data, reject, config->_invalidRequest, UNUSED);
    }
  } else {
    AccessControlDebug("failed to find cookie %s", config->_cookieName.c_str());
    remapStatus = handleInvalidToken(txnp, data, reject, config->_invalidRequest, UNUSED);
  }

  return remapStatus;
}

/**
 * @brief Remap and sets up access control based on whether access control is required, failed, etc.
 *
 * @param instance plugin instance pointer
 * @param txn transaction handle
 * @param rri remap request info pointer
 * @return TSREMAP_NO_REMAP (access validation = success)
 * TSREMAP_DID_REMAP (access validation = failure and rejection of failed requests is configured)
 */
TSRemapStatus
TSRemapDoRemap(void *instance, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  TSRemapStatus remapStatus   = TSREMAP_NO_REMAP;
  AccessControlConfig *config = static_cast<AccessControlConfig *>(instance);

  if (nullptr != config) {
    /* Plugin is designed to be used only with TLS, check the scheme */
    int schemeLen      = 0;
    const char *scheme = TSUrlSchemeGet(rri->requestBufp, rri->requestUrl, &schemeLen);
    if (nullptr != scheme) {
      if (/* strlen("https") */ 5 == schemeLen && 0 == strncmp(scheme, "https", schemeLen)) {
        AccessControlDebug("validate the access token");

        String reqPath;
        int pathLen      = 0;
        const char *path = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &pathLen);
        if (nullptr != path && 0 < pathLen) {
          reqPath.assign(path, pathLen);
        }
        /* Check if any of the uri-path multi-pattern matched and if yes enforce access control. */
        String filename;
        String pattern;
        if (config->_uriPathScope.empty()) {
          /* Scope match enforce access control */
          AccessControlDebug("no plugin scope specified, enforcing access control");
          remapStatus = enforceAccessControl(txnp, rri, config);
        } else {
          if (true == config->_uriPathScope.matchAll(reqPath, filename, pattern)) {
            AccessControlDebug("matched plugin scope enforcing access control for path %s", reqPath.c_str());

            /* Scope match enforce access control */
            remapStatus = enforceAccessControl(txnp, rri, config);
          } else {
            AccessControlDebug("not matching plugin scope (file: %s, pattern %s), skipping access control for path '%s'",
                               filename.c_str(), pattern.c_str(), reqPath.c_str());
          }
        }
      } else {
        TSHttpTxnStatusSet(txnp, config->_invalidRequest);
        AccessControlDebug("https is the only allowed scheme (plugin should be used only with TLS)");
        remapStatus = TSREMAP_DID_REMAP;
      }
    } else {
      TSHttpTxnStatusSet(txnp, config->_internalError);
      AccessControlError("failed to get request uri-scheme");
      remapStatus = TSREMAP_DID_REMAP;
    }
  } else {
    /* Something is terribly wrong, we cannot get the configuration */
    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_INTERNAL_SERVER_ERROR);
    AccessControlError("configuration unavailable");
    remapStatus = TSREMAP_DID_REMAP;
  }

  return remapStatus;
}
