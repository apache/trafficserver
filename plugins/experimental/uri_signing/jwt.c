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
#include "jwt.h"
#include "match.h"
#include "normalize.h"
#include <jansson.h>
#include <cjose/cjose.h>
#include <math.h>
#include <time.h>
#include <string.h>

double
parse_number(json_t *num)
{
  if (!json_is_number(num)) {
    return NAN;
  }
  return json_number_value(num);
}

int
parse_integer_default(json_t *num, int def)
{
  if (!json_is_integer(num)) {
    return def;
  }
  return json_integer_value(num);
}

struct jwt *
parse_jwt(json_t *raw)
{
  if (!raw) {
    return NULL;
  }

  struct jwt *jwt = malloc(sizeof *jwt);
  jwt->raw        = raw;
  jwt->iss        = json_string_value(json_object_get(raw, "iss"));
  jwt->sub        = json_string_value(json_object_get(raw, "sub"));
  jwt->aud        = json_object_get(raw, "aud");
  jwt->exp        = parse_number(json_object_get(raw, "exp"));
  jwt->nbf        = parse_number(json_object_get(raw, "nbf"));
  jwt->iat        = parse_number(json_object_get(raw, "iat"));
  jwt->jti        = json_string_value(json_object_get(raw, "jti"));
  jwt->cdniv      = parse_integer_default(json_object_get(raw, "cdniv"), 1);
  jwt->cdnicrit   = json_string_value(json_object_get(raw, "cdnicrit"));
  jwt->cdniip     = json_string_value(json_object_get(raw, "cdniip"));
  jwt->cdniuc     = json_string_value(json_object_get(raw, "cdniuc"));
  jwt->cdniets    = json_integer_value(json_object_get(raw, "cdniets"));
  jwt->cdnistt    = json_integer_value(json_object_get(raw, "cdnistt"));
  jwt->cdnistd    = parse_integer_default(json_object_get(raw, "cdnistd"), 0);
  return jwt;
}

void
jwt_delete(struct jwt *jwt)
{
  if (!jwt) {
    return;
  }

  json_decref(jwt->aud);
  json_decref(jwt->raw);
  free(jwt);
}

double
now(void)
{
  struct timespec t;
  if (!clock_gettime(CLOCK_REALTIME, &t)) {
    return (double)t.tv_sec + 1.0e-9 * (double)t.tv_nsec;
  }
  return NAN;
}

bool
unsupported_string_claim(const char *str)
{
  return !str;
}

bool
jwt_validate(struct jwt *jwt)
{
  if (!jwt) {
    PluginDebug("Initial JWT Failure: NULL argument");
    return false;
  }

  if (jwt->cdniv != 1) { /* Only support the very first version! */
    PluginDebug("Initial JWT Failure: wrong version");
    return false;
  }

  if (now() > jwt->exp) {
    PluginDebug("Initial JWT Failure: expired token");
    return false;
  }

  if (now() < jwt->nbf) {
    PluginDebug("Initial JWT Failure: nbf claim violated");
    return false;
  }

  if (!unsupported_string_claim(jwt->cdniip)) {
    PluginDebug("Initial JWT Failure: cdniip unsupported");
    return false;
  }

  if (!unsupported_string_claim(jwt->jti)) {
    PluginDebug("Initial JWT Failure: nonse unsupported");
    return false;
  }

  if (!unsupported_string_claim(jwt->cdnicrit)) {
    PluginDebug("Initial JWT Failure: cdnicrit unsupported");
    return false;
  }

  if (jwt->cdnistt < 0 || jwt->cdnistt > 1) {
    PluginDebug("Initial JWT Failure: unsupported value for cdnistt: %d", jwt->cdnistt);
    return false;
  }

  if (jwt->cdnistd != 0) {
    PluginDebug("Initial JWT Failure: unsupported value for cdnistd: %d", jwt->cdnistd);
    return false;
  }

  return true;
}

bool
jwt_check_aud(json_t *aud, const char *id)
{
  if (!aud) {
    return true;
  }
  if (!id) {
    return false;
  }
  /* If aud is a string, do a simple string comparison */
  const char *aud_str = json_string_value(aud);
  if (aud_str) {
    PluginDebug("Checking aud %s agaisnt token aud string \"%s\"", id, aud_str);
    /* Both strings will be null terminated per jansson docs */
    if (strcmp(aud_str, id) == 0) {
      return true;
    }
    return false;
  }
  PluginDebug("Checking aud %s agaisnt token aud array", id);
  /* If aud is an array, check all items */
  if (json_is_array(aud)) {
    size_t index;
    json_t *aud_item;
    json_array_foreach(aud, index, aud_item)
    {
      aud_str = json_string_value(aud_item);
      if (aud_str) {
        if (strcmp(aud_str, id) == 0) {
          return true;
        }
      }
    }
  }
  return false;
}

bool
jwt_check_uri(const char *cdniuc, const char *uri)
{
  static const char CONT_URI_HASH_STR[]  = "hash";
  static const char CONT_URI_REGEX_STR[] = "regex";

  /* If cdniuc is not provided, skip uri check */
  if (!cdniuc || !*cdniuc) {
    return true;
  }

  if (!uri) {
    return false;
  }

  /* Normalize the URI */
  int uri_ct  = strlen(uri);
  int buff_ct = uri_ct + 2;
  int err;
  char *normal_uri = (char *)TSmalloc(buff_ct);
  memset(normal_uri, 0, buff_ct);

  err = normalize_uri(uri, uri_ct, normal_uri, buff_ct);

  if (err) {
    goto fail_jwt;
  }

  const char *kind = cdniuc, *container = cdniuc;
  while (*container && *container != ':') {
    ++container;
  }
  if (!*container) {
    goto fail_jwt;
  }
  ++container;

  size_t len = container - kind;
  bool status;
  PluginDebug("Comparing with match kind \"%.*s\" on \"%s\" to normalized URI \"%s\"", (int)len - 1, kind, container, normal_uri);
  switch (len) {
  case sizeof CONT_URI_HASH_STR:
    if (!strncmp(CONT_URI_HASH_STR, kind, len - 1)) {
      status = match_hash(container, normal_uri);
      TSfree(normal_uri);
      return status;
    }
    PluginDebug("Expected kind %s, but did not find it in \"%.*s\"", CONT_URI_HASH_STR, (int)len - 1, kind);
    break;
  case sizeof CONT_URI_REGEX_STR:
    if (!strncmp(CONT_URI_REGEX_STR, kind, len - 1)) {
      status = match_regex(container, normal_uri);
      TSfree(normal_uri);
      return status;
    }
    PluginDebug("Expected kind %s, but did not find it in \"%.*s\"", CONT_URI_REGEX_STR, (int)len - 1, kind);
    break;
  }
  PluginDebug("Unknown match kind \"%.*s\"", (int)len - 1, kind);

fail_jwt:
  TSfree(normal_uri);
  return false;
}

void
renew_copy_string(json_t *new_json, const char *name, const char *old)
{
  if (old) {
    json_object_set_new(new_json, name, json_string(old));
  }
}

void
renew_copy_raw(json_t *new_json, const char *name, json_t *old_json)
{
  if (old_json) {
    json_object_set_new(new_json, name, old_json);
  }
}

void
renew_copy_real(json_t *new_json, const char *name, double old)
{
  if (!isnan(old)) {
    json_object_set_new(new_json, name, json_real(old));
  }
}

void
renew_copy_integer(json_t *new_json, const char *name, double old)
{
  /* Integers have no sentinel value and cannot be missing. */
  json_object_set_new(new_json, name, json_integer(old));
}

char *
renew(struct jwt *jwt, const char *iss, cjose_jwk_t *jwk, const char *alg, const char *package)
{
  char *s = NULL;
  if (jwt->cdnistt != 1) {
    PluginDebug("Not renewing jwt, cdnistt != 1");
    return NULL;
  }

  if (jwt->cdniets == 0) {
    PluginDebug("Not renewing jwt, cdniets == 0");
    return NULL;
  }

  json_t *new_json = json_object();
  renew_copy_string(new_json, "iss", iss); /* use issuer of new signing key */
  renew_copy_string(new_json, "sub", jwt->sub);
  renew_copy_raw(new_json, "aud", jwt->aud);
  renew_copy_real(new_json, "exp", now() + jwt->cdniets); /* expire ets seconds hence */
  renew_copy_real(new_json, "nbf", jwt->nbf);
  renew_copy_real(new_json, "iat", now()); /* issued now */
  renew_copy_string(new_json, "jti", jwt->jti);
  renew_copy_string(new_json, "cdniuc", jwt->cdniuc);
  renew_copy_integer(new_json, "cdniv", jwt->cdniv);
  renew_copy_integer(new_json, "cdniets", jwt->cdniets);
  renew_copy_integer(new_json, "cdnistt", jwt->cdnistt);
  renew_copy_integer(new_json, "cdnistd", jwt->cdnistd);

  char *pt = json_dumps(new_json, JSON_COMPACT);
  json_decref(new_json);

  cjose_header_t *hdr = cjose_header_new(NULL);
  if (!hdr) {
    PluginDebug("Unable to create new jose header.");
    goto fail_json;
  }

  cjose_err err;
  const char *kid = cjose_jwk_get_kid(jwk, &err);
  if (!kid) {
    PluginDebug("Unable to get kid from signing key: %s", err.message);
    goto fail_hdr;
  }
  if (!cjose_header_set(hdr, CJOSE_HDR_KID, kid, &err)) {
    PluginDebug("Unable to set kid of jose header to %s: %s", kid, err.message);
    goto fail_hdr;
  }
  if (!cjose_header_set(hdr, "alg", alg, &err)) {
    PluginDebug("Unable to set alg of jose header to %s: %s", alg, err.message);
    goto fail_hdr;
  }

  cjose_jws_t *jws = cjose_jws_sign(jwk, hdr, (uint8_t *)pt, strlen(pt), &err);
  if (!jws) {
    char *hdr_str = json_dumps((json_t *)hdr, JSON_COMPACT);
    PluginDebug("Unable to sign new key: %s. {%p(%s), \"%s\", \"%s\"}", err.message, jwk, kid, hdr_str, pt);
    free(hdr_str);
    goto fail_hdr;
  }

  const char *jws_str;
  if (!cjose_jws_export(jws, &jws_str, &err)) {
    PluginDebug("Unable to export jws: %s", err.message);
    goto fail_jws;
  }

  const char *fmt = "%s=%s";
  size_t s_ct;
  s = malloc(s_ct = (1 + snprintf(NULL, 0, fmt, package, jws_str)));
  snprintf(s, s_ct, fmt, package, jws_str);
fail_jws:
  cjose_jws_release(jws);
fail_hdr:
  cjose_header_release(hdr);
fail_json:
  free(pt);
  return s;
}
