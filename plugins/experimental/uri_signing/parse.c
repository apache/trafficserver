/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common.h"
#include "parse.h"
#include "config.h"
#include "jwt.h"
#include "cookie.h"
#include "timing.h"
#include <cjose/cjose.h>
#include <jansson.h>
#include <string.h>
#include <ts/ts.h>
#include <inttypes.h>

cjose_jws_t *
get_jws_from_uri(const char *uri, size_t uri_ct, const char *paramName, char *strip_uri, size_t buff_ct, size_t *strip_ct)
{
  /* Reserved characters as defined by the URI Generic Syntax RFC: https://tools.ietf.org/html/rfc3986#section-2.2 */
  static const char *reserved_string  = ":/?#[]@!$&\'()*+,;=";
  static const char *sub_delim_string = "!$&\'()*+,;=";

  /* If param name ends in reserved character this will be treated as the termination symbol when parsing for package. Default is
   * '='. */
  char termination_symbol;
  size_t termination_ct;
  size_t param_ct = strlen(paramName);

  if (param_ct <= 0) {
    PluginDebug("URI signing package name cannot be empty");
    return NULL;
  }

  if (strchr(reserved_string, paramName[param_ct - 1])) {
    termination_symbol = paramName[param_ct - 1];
    termination_ct     = param_ct - 1;
  } else {
    termination_symbol = '=';
    termination_ct     = param_ct;
  }

  PluginDebug("Parsing JWS from query string: %.*s", (int)uri_ct, uri);
  const char *param = uri;
  const char *end   = uri + uri_ct;
  const char *key, *key_end;
  const char *value, *value_end;

  for (;;) {
    /* Search the URI for a reserved character. */
    while (param != end && strchr(reserved_string, *param) == NULL) {
      ++param;
    }
    if (param == end) {
      break;
    }

    ++param;

    /* Parse the parameter for a key value pair separated by the termination symbol. */
    key   = param;
    value = param;
    while (value != end && *value != termination_symbol) {
      ++value;
    }
    if (value == end) {
      break;
    }
    key_end = value;

    /* If the Parameter key is our target parameter name, attempt to import a JWS from the value. */
    if ((size_t)(key_end - key) == termination_ct && !strncmp(paramName, key, (size_t)(key_end - key))) {
      value_end = ++value;
      while (value_end != end && strchr(reserved_string, *value_end) == NULL) {
        ++value_end;
      }
      PluginDebug("Decoding JWS: %.*s", (int)(key_end - key), key);
      cjose_err err    = {0};
      cjose_jws_t *jws = cjose_jws_import(value, (size_t)(value_end - value), &err);
      if (!jws) {
        PluginDebug("Unable to read JWS: %.*s, %s", (int)(key_end - key), key, err.message ? err.message : "");
      } else {
        PluginDebug("Parsed JWS: %.*s (%16p)", (int)(key_end - key), key, jws);

        /* Strip token */
        /* Check that passed buffer is large enough */
        *strip_ct = ((key - uri) + (end - value_end));
        if (buff_ct <= *strip_ct) {
          PluginDebug("Strip URI buffer is not large enough");
          return NULL;
        }

        if (value_end != end && strchr(sub_delim_string, *value_end)) {
          /*Strip from first char of package name to sub-delimeter that terminates the signed JWT */
          memcpy(strip_uri, uri, (key - uri));
          memcpy(strip_uri + (key - uri), value_end + 1, (end - value_end + 1));
        } else {
          /*Strip from reserved char to the last char of the JWT */
          memcpy(strip_uri, uri, (key - uri - 1));
          memcpy(strip_uri + (key - uri - 1), value_end, (end - value_end));
        }

        if (strip_uri[*strip_ct - 1] != '\0') {
          strip_uri[*strip_ct - 1] = '\0';
        }
        PluginDebug("Stripped URI: %s", strip_uri);
      }
      return jws;
    }
  }
  PluginDebug("Unable to locate signing key in uri: %.*s", (int)uri_ct, uri);
  return NULL;
}

cjose_jws_t *
get_jws_from_cookie(const char **cookie, size_t *cookie_ct, const char *paramName)
{
  PluginDebug("Parsing JWS from cookie: %.*s", (int)*cookie_ct, *cookie);
  size_t value_ct;
  const char *value = get_cookie_value(cookie, cookie_ct, paramName, &value_ct);
  PluginDebug("Got jws string: (%p) %.*s", value, (int)value_ct, value);
  if (!value || !value_ct) {
    return NULL;
  }
  cjose_err err    = {0};
  cjose_jws_t *jws = cjose_jws_import(value, value_ct, &err);
  if (!jws) {
    PluginDebug("Unable to read JWS: %.*s, %s", (int)value_ct, value, err.message ? err.message : "");
  } else {
    PluginDebug("Parsed JWS: %.*s (%16p)", (int)value_ct, value, jws);
  }
  return jws;
}

struct jwt *
validate_jws(cjose_jws_t *jws, struct config *cfg, const char *uri, size_t uri_ct)
{
  struct timer t;
  int64_t last_mark = 0;
  start_timer(&t);

#define TimerDebug(msg)                                             \
  do {                                                              \
    int64_t new_mark = mark_timer(&t);                              \
    PluginDebug("Spent %" PRId64 " ns " msg, new_mark - last_mark); \
    last_mark = new_mark;                                           \
  } while (0)

  PluginDebug("Validating JWS for %16p", jws);
  cjose_err cerr = {0};
  size_t pt_ct;
  const char *pt;
  if (!cjose_jws_get_plaintext(jws, (uint8_t **)&pt, &pt_ct, &cerr)) {
    PluginDebug("Cannot get plaintext for %16p", jws);
    return false;
  }

  TimerDebug("getting jws plaintext");

  json_error_t jerr = {0};
  struct jwt *jwt   = parse_jwt(json_loadb(pt, pt_ct, 0, &jerr));
  TimerDebug("parsing jwt");
  if (!jwt) {
    if (jerr.text[0]) {
      PluginDebug("Cannot parse json for %16p: %.*s '%s'", jws, (int)pt_ct, pt, jerr.text);
    } else {
      PluginDebug("Cannot parse jwt for %16p: %.*s", jws, (int)pt_ct, pt);
    }
    return NULL;
  }

  if (!jwt_validate(jwt)) {
    PluginDebug("Initial validation of JWT failed for %16p", jws);
    goto jwt_fail;
  }
  TimerDebug("initial validation of jwt");

  cjose_header_t *hdr = cjose_jws_get_protected(jws);
  TimerDebug("getting header of jws");
  if (!hdr) {
    PluginDebug("Cannot get protected header for %16p", jws);
    goto jwt_fail;
  }

  const char *kid = cjose_header_get(hdr, "kid", NULL);
  TimerDebug("getting kid of jws header");
  if (kid) {
    cjose_jwk_t *jwk = find_key_by_kid(cfg, jwt->iss, kid);
    TimerDebug("finding key for jwt");
    if (!jwk) {
      PluginDebug("Cannot find key %s for issuer %s for %16p", kid, jwt->iss, jws);
      goto jwt_fail;
    }
    if (!cjose_jws_verify(jws, jwk, NULL)) {
      PluginDebug("Key %s for issuer %s for %16p does not validate.", kid, jwt->iss, jws);
      goto jwt_fail;
    }
    TimerDebug("checking crypto signature for jwt");
  } else {
    PluginDebug("Searching all keys for issuer %s for %16p", jwt->iss, jws);
    cjose_jwk_t **jwks;
    for (jwks = find_keys(cfg, jwt->iss); jwks && *jwks; ++jwks) {
      if (cjose_jws_verify(jws, *jwks, NULL)) {
        break;
      }
    }
    TimerDebug("checking the crypto signature of all possible keys for jwt");
    if (!jwks || !*jwks) {
      if (!jwks) {
        PluginDebug("No keys found for issuer %s for %16p.", jwt->iss, jws);
      } else {
        PluginDebug("No valid key for issuer %s found for %16p", jwt->iss, jws);
      }
      goto jwt_fail;
    }
  }

  if (!jwt_check_aud(jwt->aud, config_get_id(cfg))) {
    PluginDebug("Valid key for %16p that does not match aud.", jws);
    goto jwt_fail;
  }

  if (!jwt_check_uri(jwt->cdniuc, uri)) {
    PluginDebug("Valid key for %16p that does not match uri.", jws);
    goto jwt_fail;
  }
  TimerDebug("verifying sub claim");

  return jwt;
jwt_fail:
  jwt_delete(jwt);
  return NULL;
}
