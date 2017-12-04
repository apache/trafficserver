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

#include "uri_signing.h"
#include "config.h"
#include "timing.h"

#include <ts/ts.h>

#include <cjose/cjose.h>
#include <jansson.h>

#include <string.h>
#include <search.h>
#include <errno.h>

#define JSONError(err) PluginError("json-err: %s:%d:%d: %s", (err).source, (err).line, (err).column, (err).text)

struct config {
  struct hsearch_data *issuers;
  cjose_jwk_t ***jwkis;
  char **issuer_names;
  struct signer signer;
};

cjose_jwk_t **
find_keys(struct config *cfg, const char *issuer)
{
  ENTRY *entry;
  if (!hsearch_r((ENTRY){.key = (char *)issuer}, FIND, &entry, cfg->issuers) || !entry) {
    PluginDebug("Unable to locate any keys at %p for issuer %s in %p->%p", entry, issuer, cfg, cfg->issuers);
    return NULL;
  }
  int n = 0;
  for (cjose_jwk_t **jwks = entry->data; *jwks; ++jwks, ++n) {
    ;
  }
  PluginDebug("Located %d keys for issuer %s in %p->%p", n, issuer, cfg, cfg->issuers);
  return entry->data;
}

cjose_jwk_t *
find_key_by_kid(struct config *cfg, const char *issuer, const char *kid)
{
  const char *this_kid;
  cjose_jwk_t **jwkis = find_keys(cfg, issuer);
  if (!jwkis) {
    return NULL;
  }
  for (cjose_jwk_t **jwks = jwkis; *jwks; ++jwks) {
    if ((this_kid = cjose_jwk_get_kid(*jwks, NULL)) && !strcmp(this_kid, kid)) {
      return *jwks;
    }
  }
  return NULL;
}

struct config *
config_new(size_t n)
{
  PluginDebug("Creating new config object with size %ld", n);
  struct config *cfg = malloc(sizeof *cfg);

  cfg->issuers = calloc(1, sizeof *cfg->issuers);
  if (!hcreate_r(n * 2, cfg->issuers)) {
    PluginError("Unable to create config table (%d)!", errno);
    free(cfg);
    return NULL;
  }
  PluginDebug("Created table with size %d", cfg->issuers->size);

  cfg->jwkis    = malloc((n + 1) * sizeof *cfg->jwkis);
  cfg->jwkis[n] = NULL;

  cfg->issuer_names    = malloc((n + 1) * sizeof *cfg->issuer_names);
  cfg->issuer_names[n] = NULL;

  cfg->signer.issuer = NULL;
  cfg->signer.jwk    = NULL;
  cfg->signer.alg    = NULL;

  PluginDebug("New config object created at %p", cfg);
  return cfg;
}

void
config_delete(struct config *cfg)
{
  if (!cfg) {
    return;
  }
  hdestroy_r(cfg->issuers);

  for (cjose_jwk_t ***jwkis = cfg->jwkis; *jwkis; ++jwkis) {
    for (cjose_jwk_t **jwks = *jwkis; *jwks; ++jwks) {
      cjose_jwk_release(*jwks);
    }
    free(*jwkis);
  }
  free(cfg->jwkis);

  for (char **name = cfg->issuer_names; *name; ++name) {
    free(*name);
  }
  free(cfg->issuer_names);

  if (cfg->signer.alg) {
    free(cfg->signer.alg);
  }
  free(cfg);
}

cjose_jwk_t *
load_jwk(json_t *obj, cjose_err *err)
{
  char *s = json_dumps(obj, JSON_COMPACT);
  if (!s) {
    PluginError("Failed to re-serialize JSON sub-object.");
    return NULL;
  }

  cjose_jwk_t *jwk = cjose_jwk_import(s, strlen(s), err);
  free(s);
  return jwk;
}

struct config *
read_config(const char *path)
{
  json_error_t err    = {0};
  json_t *issuer_json = json_load_file(path, 0, &err);
  if (!issuer_json) {
    JSONError(err);
    goto fail;
  }

  if (!json_is_object(issuer_json)) {
    PluginError("Config file is not a valid JSON object");
    goto issuer_fail;
  }

  size_t issuers_ct = json_object_size(issuer_json);
  if (!issuers_ct) {
    PluginError("Config file contains no issuers.");
    goto issuer_fail;
  }

  struct config *cfg = config_new(issuers_ct);
  if (!cfg) {
    PluginError("Unable to allocate config.");
    goto issuer_fail;
  }

  cjose_jwk_t ***jwkis = cfg->jwkis;
  char **issuer        = cfg->issuer_names;
  const char *json_issuer;
  json_t *jwks;
  json_object_foreach(issuer_json, json_issuer, jwks)
  {
    *issuer         = strdup(json_issuer);
    json_t *key_ary = json_object_get(jwks, "keys");
    if (!key_ary) {
      PluginError("Failed to get keys member from jwk for issuer %s", *issuer);
      *jwkis = NULL;
      goto cfg_fail;
    }
    PluginDebug("Created table with size %d", cfg->issuers->size);

    const char *renewal_kid  = NULL;
    json_t *renewal_kid_json = json_object_get(jwks, "renewal_kid");
    if (renewal_kid_json) {
      renewal_kid = json_string_value(renewal_kid_json);
    }

    size_t jwks_ct     = json_array_size(key_ary);
    cjose_jwk_t **jwks = (*jwkis++ = malloc((jwks_ct + 1) * sizeof *jwks));
    PluginDebug("Created table with size %d", cfg->issuers->size);
    if (!hsearch_r(((ENTRY){(char *)*issuer, jwks}), ENTER, &(ENTRY *){0}, cfg->issuers)) {
      PluginDebug("Failed to store keys for issuer %s", *issuer);
    } else {
      PluginDebug("Stored keys for %s at %16p", *issuer, jwks);
    }

    json_t *jwk_obj;
    cjose_err jwk_err = {0};
    for (size_t idx = 0; (idx < jwks_ct) && (jwk_obj = json_array_get(key_ary, idx)); ++idx, ++jwks) {
      if ((*jwks = load_jwk(jwk_obj, &jwk_err))) {
        const char *kid = cjose_jwk_get_kid(*jwks, NULL);
        PluginDebug("Stored jwk %ld for issuer %s, kid %s, cfg %p->%p", idx, *issuer, kid ? kid : "<no kid>", cfg, cfg->issuers);
        if (renewal_kid && kid && !strcmp(kid, renewal_kid)) {
          if (cfg->signer.issuer) {
            PluginError("Cannot load multiple renewal keys for a single remap. iss:\"%s\", kid:\"%s\"; iss:\"%s\", kid:\"%s\"",
                        cfg->signer.issuer, cjose_jwk_get_kid(cfg->signer.jwk, NULL), *issuer, kid);
            goto cfg_fail;
          } else {
            cfg->signer.issuer = *issuer;
            cfg->signer.jwk    = *jwks;

            const char *jwk_alg = json_string_value(json_object_get(jwk_obj, "alg"));
            if (!jwk_alg) {
              PluginError("Cannot load JWK algorithm for renewal key.");
              goto cfg_fail;
            }
            cfg->signer.alg = strdup(jwk_alg);
          }
        }
      } else {
        PluginError("Failed to load jwk %ld for issuer %s: %s", idx, *issuer, jwk_err.message);
        goto cfg_fail;
      }
    }
    *jwks = NULL;
    ++issuer;
  }
  if (!cfg->signer.issuer) {
    PluginError("Cannot load remap without signing key.");
    goto cfg_fail;
  }
  json_decref(issuer_json);
  PluginDebug("Loaded config file successfully.");
  return cfg;
cfg_fail:
  config_delete(cfg);
issuer_fail:
  json_decref(issuer_json);
fail:
  return NULL;
}

struct signer *
config_signer(struct config *cfg)
{
  if (!cfg) {
    return NULL;
  }
  return &cfg->signer;
}
