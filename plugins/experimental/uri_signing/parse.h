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

#pragma once

#include <stdlib.h>

struct _cjose_jws_int;

/* For now strip_ct returns size of string *including* the null terminator */
struct _cjose_jws_int *get_jws_from_uri(const char *uri, size_t uri_ct, const char *paramName, char *strip_uri, size_t buff_ct,
                                        size_t *strip_ct);
struct _cjose_jws_int *get_jws_from_cookie(const char **cookie, size_t *cookie_ct, const char *paramName);

struct config;
struct jwt;
struct jwt *validate_jws(struct _cjose_jws_int *jws, struct config *cfg, const char *uri, size_t uri_ct);
