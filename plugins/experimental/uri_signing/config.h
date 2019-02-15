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

#include <stdbool.h>
#include <stdlib.h>

struct config;
struct _cjose_jwk_int;
struct signer {
  char *issuer;
  struct _cjose_jwk_int *jwk;
  char *alg;
};

struct config *read_config(const char *path);
void config_delete(struct config *g);
struct signer *config_signer(struct config *);
struct _cjose_jwk_int **find_keys(struct config *cfg, const char *issuer);
struct _cjose_jwk_int *find_key_by_kid(struct config *cfg, const char *issuer, const char *kid);
bool uri_matches_auth_directive(struct config *cfg, const char *uri, size_t uri_ct);
const char *config_get_id(struct config *cfg);
