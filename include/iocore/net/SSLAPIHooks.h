/** @file

  Internal SDK stuff

  @section license License

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

#pragma once

#include "api/FeatureAPIHooks.h"

#include "ts/apidefs.h"

#include <memory>

class TSSslHookInternalID
{
public:
  explicit constexpr TSSslHookInternalID(TSHttpHookID id) : _id(id - TS_SSL_FIRST_HOOK) {}

  constexpr
  operator int() const
  {
    return _id;
  }

  static const int NUM = TS_SSL_LAST_HOOK - TS_SSL_FIRST_HOOK + 1;

  constexpr bool
  is_in_bounds() const
  {
    return (_id >= 0) && (_id < NUM);
  }

private:
  const int _id;
};

class SSLAPIHooks : public FeatureAPIHooks<TSSslHookInternalID, TSSslHookInternalID::NUM>
{
};

// there is no corresponding deinit; we leak the resource on shutdown
void init_global_ssl_hooks();

extern std::unique_ptr<SSLAPIHooks> g_ssl_hooks;
