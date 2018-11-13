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
#include <jansson.h>

struct jwt {
  json_t *raw;
  const char *iss;
  const char *sub;
  const char *aud;
  double exp;
  double nbf;
  double iat;
  const char *jti;
  int cdniv;
  const char *cdnicrit;
  const char *cdniip;
  int cdniets;
  int cdnistt;
  int cdnistd;
};
struct jwt *parse_jwt(json_t *raw);
void jwt_delete(struct jwt *jwt);
bool jwt_validate(struct jwt *jwt);
bool jwt_check_uri(const char *sub, const char *uri);

struct _cjose_jwk_int;
char *renew(struct jwt *jwt, const char *iss, struct _cjose_jwk_int *jwk, const char *alg, const char *package);
